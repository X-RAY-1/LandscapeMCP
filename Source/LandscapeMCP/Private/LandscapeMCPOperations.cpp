#include "LandscapeMCPOperations.h"
#include "LandscapeMCPTerrainAnalysis.h"
#include "LandscapeMCPHeightfieldAccess.h"
#include "LandscapeMCPPathSampling.h"
#include "LandscapeMCPApply.h"
#include "LandscapeMCPCollisionModel.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Chaos/HeightField.h"
#endif
#include "Editor/Transactor.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionLandscapeGrassOutput.h"
#include "Serialization/ObjectWriter.h"
#include "Misc/Crc.h"
#include "GameFramework/WorldSettings.h"
#include "Settings/EditorLoadingSavingSettings.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "InstancedFoliageActor.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeInfo.h"
#include "LandscapeStreamingProxy.h"
#include "LevelUtils.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

namespace LandscapeMCP
{
namespace
{
    constexpr int32 MaxEditSamples = 16384;
    constexpr int32 MaxCreateSamples = 262144;
    constexpr double MaxCoordinate = 10000000.0;
    constexpr double MaxRadius = 5000.0;
    constexpr double MaxSculpt = 1000.0;

    FLandscapeMCPResult Fail(const FString& Message, const FString& Path = FString(), bool bDryRun = false)
    {
        FLandscapeMCPResult R; R.Message = Message; R.LandscapePath = Path; R.bDryRun = bDryRun; return R;
    }
    bool Coordinate(double V) { return FMath::IsFinite(V) && FMath::Abs(V) <= MaxCoordinate; }
    bool VectorValid(FVector V) { return Coordinate(V.X) && Coordinate(V.Y) && Coordinate(V.Z); }
    bool EditorReady(FString& Error)
    {
        if (!IsInGameThread() || !GEditor || !GEditor->GetEditorWorldContext().World())
        { Error = TEXT("An Editor world on the game thread is required."); return false; }
        if (GEditor->PlayWorld || GEditor->IsPlayingSessionInEditor() || UE::IsSavingPackage() || IsGarbageCollecting())
        { Error = TEXT("Editing/reading is unavailable during PIE, saving or garbage collection."); return false; }
        return true;
    }
    bool LevelValid(ULevel* Level, FString& Error)
    {
        if (!Level || Level->GetWorld() != GEditor->GetEditorWorldContext().World())
        { Error = TEXT("Exact Level path must identify a loaded Level in the current Editor world."); return false; }
        if (Level->GetWorld()->GetWorldPartition() || Level->IsUsingExternalActors())
        { Error = TEXT("World Partition / external-actor Levels are unsupported."); return false; }
        if (!Level->bIsVisible || FLevelUtils::IsLevelLocked(Level))
        { Error = TEXT("Hidden or locked Levels are unsupported."); return false; }
        return true;
    }
    struct FTarget
    {
        ALandscape* Actor = nullptr;
        ULandscapeInfo* Info = nullptr;
        FGuid LayerGuid;
        FIntRect Extent; // GetLandscapeExtentの契約に合わせ、最大座標を含む。
        FVector Scale;
        FVector Origin;
    };
    bool Resolve(const FString& Path, FTarget& T, FString& Error)
    {
        if (!EditorReady(Error)) { return false; }
        if (!Path.StartsWith(TEXT("/")) || !Path.Contains(TEXT(":PersistentLevel.")))
        { Error = TEXT("LandscapePath must be an exact loaded Actor object path, not a name or label."); return false; }
        ALandscapeProxy* Proxy = FindObject<ALandscapeProxy>(nullptr, *Path);
        if (!Proxy || Proxy->GetPathName() != Path || !IsValid(Proxy))
        { Error = TEXT("Landscape not found at exact path; no Actor was created."); return false; }
        T.Actor = Cast<ALandscape>(Proxy);
        if (!T.Actor || T.Actor->GetClass() != ALandscape::StaticClass())
        { Error = TEXT("Streaming proxies and Landscape subclasses are unsupported."); return false; }
        if (!LevelValid(T.Actor->GetLevel(), Error)) { return false; }
        for (TActorIterator<ALandscapeProxy> It(T.Actor->GetWorld()); It; ++It)
        {
            if (*It != T.Actor && It->GetLandscapeGuid() == T.Actor->GetLandscapeGuid())
            { Error = TEXT("Landscape has another proxy; streaming landscapes are unsupported."); return false; }
        }
        T.Scale = T.Actor->GetActorScale3D(); T.Origin = T.Actor->GetActorLocation();
        if (!VectorValid(T.Origin) || !T.Actor->GetActorQuat().Equals(FQuat::Identity, 1.e-8)
            || !VectorValid(T.Scale) || T.Scale.GetMin() < 1 || T.Scale.GetMax() > 1000)
        { Error = TEXT("Only unrotated Landscapes with positive scales in [1,1000] are supported."); return false; }
        if (T.Actor->IsNaniteEnabled()) { Error = TEXT("Nanite Landscapes are unsupported."); return false; }
        const TArray<ULandscapeEditLayerBase*> Layers = T.Actor->GetEditLayers();
        if (Layers.Num() != 1 || !Layers[0] || Layers[0]->GetClass() != ULandscapeEditLayer::StaticClass()
            || Layers[0]->IsLocked() || !Layers[0]->IsVisible()
            || !FMath::IsNearlyEqual(Layers[0]->GetAlphaForTargetType(ELandscapeToolTargetType::Heightmap), 1.0f))
        { Error = TEXT("Requires exactly one visible, unlocked standard Edit Layer with height alpha 1."); return false; }
        const FLandscapeLayer* Layer = T.Actor->GetLayerConst(0);
        if (!Layer || !Layer->Brushes.IsEmpty())
        { Error = TEXT("Blueprint brushes / procedural layer contributions are unsupported."); return false; }
        T.LayerGuid = Layers[0]->GetGuid();
        T.Info = T.Actor->GetLandscapeInfo();
        if (!T.Info || !T.Info->GetLandscapeExtent(T.Extent) || T.Actor->LandscapeComponents.IsEmpty()
            || T.Actor->LandscapeComponents.Num() > 64)
        { Error = TEXT("Missing or excessively large Landscape component data."); return false; }
        const int64 Width = int64(T.Extent.Max.X) - T.Extent.Min.X + 1;
        const int64 Height = int64(T.Extent.Max.Y) - T.Extent.Min.Y + 1;
        if (Width < 2 || Height < 2 || Width * Height > MaxCreateSamples)
        { Error = TEXT("Landscape exceeds the supported total sample limit."); return false; }
        for (ULandscapeComponent* C : T.Actor->LandscapeComponents)
        {
            if (!C || C->GetLandscapeProxy() != T.Actor || !C->GetLayerData(T.LayerGuid))
            { Error = TEXT("Missing or foreign component/layer data."); return false; }
            // 高さの変更で付属Foliageが移動し得るため、副作用で他のActorを変更しない。
            if (C->GetCollisionComponent() && AInstancedFoliageActor::HasFoliageAttached(C->GetCollisionComponent()))
            { Error = TEXT("Attached foliage is unsupported: editing could move other Actors."); return false; }
            for (const FWeightmapLayerAllocationInfo& Allocation : C->GetWeightmapLayerAllocations(T.LayerGuid))
            {
                if (Allocation.LayerInfo == ALandscapeProxy::VisibilityLayer)
                { Error = TEXT("Landscapes with visibility/hole data are unsupported."); return false; }
            }
        }
        return true;
    }
    // FLandscapeEditDataInterfaceは読み取り時にもTexture::Modifyを呼び出す。
    // 元のmipデータを直接コピーし、GetHeightとDry-runでPackageやUndo履歴を変更しない。
    class FSourceReader
    {
    public:
        explicit FSourceReader(const FTarget& InTarget, int64 InByteLimit = MAX_int64) : T(InTarget), ByteLimit(InByteLimit) {}
        int64 GetCachedBytes() const { return CachedBytes; }
        bool Read(int32 X, int32 Y, uint16& Value, FString& Error, ULandscapeComponent* ExactComponent = nullptr)
        {
            ULandscapeComponent* Component = ExactComponent;
            if (!Component) for (ULandscapeComponent* C : T.Actor->LandscapeComponents)
            {
                const FIntPoint Base = C->GetSectionBase();
                if (X >= Base.X && Y >= Base.Y && X <= Base.X + C->ComponentSizeQuads && Y <= Base.Y + C->ComponentSizeQuads)
                { Component = C; break; }
            }
            if (!Component) { Error = TEXT("Missing heightfield sample / component hole."); return false; }
            UTexture2D* Texture = Component->GetHeightmap(T.LayerGuid);
            if (!Texture || Texture->GetOutermost() != T.Actor->GetLevel()->GetOutermost()
                || Texture->Source.GetFormat() != TSF_BGRA8 || Texture->Source.GetSizeX() > 1024 || Texture->Source.GetSizeY() > 1024)
            { Error = TEXT("Unsupported, external or oversized source heightmap texture."); return false; }
            TArray64<uint8>* Bytes = Cache.Find(Texture);
            if (!Bytes)
            {
                // 複製・破損したLandscapeが別のActorとHeightmapを共有している場合がある。
                // Package所有関係だけでは不十分なため、Landscape間のTexture共有を拒否する。
                UTexture2D* FinalTexture = Component->GetHeightmap();
                bool bShared = false;
                for (TActorIterator<ALandscapeProxy> It(T.Actor->GetWorld()); It; ++It)
                {
                    if (*It == T.Actor) { continue; }
                    for (ULandscapeComponent* Other : It->LandscapeComponents)
                    {
                        if (!Other) { continue; }
                        bShared |= Other->GetHeightmap() == Texture || Other->GetHeightmap() == FinalTexture;
                        Other->ForEachLayer([&](const FGuid& Guid, FLandscapeLayerComponentData& Data)
                        { bShared |= Data.HeightmapData.Texture == Texture || Data.HeightmapData.Texture == FinalTexture; });
                    }
                }
                if (bShared) { Error = TEXT("Heightmap texture is shared by another Landscape; edit refused."); return false; }
                const int64 ByteCount=int64(Texture->Source.GetSizeX())*Texture->Source.GetSizeY()*sizeof(FColor);
                if(ByteCount>ByteLimit-CachedBytes)
                { Error=TEXT("SAMPLE_LIMIT_EXCEEDED: cached source mip budget exceeded.");return false; }
                TArray64<uint8> Copy;
                if (!Texture->Source.GetMipData(Copy, 0) || Copy.Num() != int64(Texture->Source.GetSizeX()) * Texture->Source.GetSizeY() * sizeof(FColor))
                { Error = TEXT("Unable to read complete source heightmap mip."); return false; }
                Bytes = &Cache.Add(Texture, MoveTemp(Copy));
                CachedBytes+=ByteCount;
            }
            const FIntPoint Local = FIntPoint(X,Y) - Component->GetSectionBase();
            const int32 SX = FMath::Min(Local.X / Component->SubsectionSizeQuads, Component->NumSubsections - 1);
            const int32 SY = FMath::Min(Local.Y / Component->SubsectionSizeQuads, Component->NumSubsections - 1);
            const int32 TX = int32(Component->HeightmapScaleBias.Z * Texture->Source.GetSizeX()) + Local.X + SX;
            const int32 TY = int32(Component->HeightmapScaleBias.W * Texture->Source.GetSizeY()) + Local.Y + SY;
            if (TX < 0 || TY < 0 || TX >= Texture->Source.GetSizeX() || TY >= Texture->Source.GetSizeY())
            { Error = TEXT("Invalid source texture coordinates."); return false; }
            const FColor* Colors = reinterpret_cast<const FColor*>(Bytes->GetData());
            const FColor& Color = Colors[TX + TY * Texture->Source.GetSizeX()];
            Value = (uint16(Color.R) << 8) | Color.G; return true;
        }
        bool Region(const FIntRect& Rect, TArray<uint16>& Values, FString& Error, ULandscapeComponent* ExactComponent = nullptr)
        {
            Values.SetNumUninitialized((Rect.Width()+1)*(Rect.Height()+1));
            int32 Index = 0;
            for (int32 Y = Rect.Min.Y; Y <= Rect.Max.Y; ++Y)
                for (int32 X = Rect.Min.X; X <= Rect.Max.X; ++X)
                    if (!Read(X,Y,Values[Index++],Error,ExactComponent)) { return false; }
            return true;
        }
    private:
        const FTarget& T;
        TMap<UTexture2D*, TArray64<uint8>> Cache;
        int64 ByteLimit = MAX_int64;
        int64 CachedBytes = 0;
    };
    double WorldHeight(const FTarget& T, double Encoded) { return T.Origin.Z + (Encoded - 32768.0) * T.Scale.Z / 128.0; }
    bool Encode(double WorldZ, double OriginZ, double ScaleZ, uint16& Out)
    {
        const double Encoded = 32768.0 + (WorldZ-OriginZ)*128.0/ScaleZ;
        if (!FMath::IsFinite(Encoded) || Encoded < 0 || Encoded > 65535) { return false; }
        Out = uint16(FMath::RoundToInt(Encoded)); return true;
    }
    bool Contains(const FIntRect& E, FVector2D P)
    { return P.X >= E.Min.X && P.Y >= E.Min.Y && P.X <= E.Max.X && P.Y <= E.Max.Y; }
    double BrushWeight(double Distance, double Radius, double Falloff)
    {
        if (Distance > Radius) { return 0; }
        if (Falloff <= 0 || Distance <= Radius*(1.0-Falloff)) { return 1; }
        const double T = FMath::Clamp((Radius-Distance)/(Radius*Falloff),0.0,1.0);
        return T*T*(3.0-2.0*T);
    }
}

FLandscapeMCPResult Create(const FString& LevelPath, const FString& Name, FVector Location, FVector Scale,
    int32 CountX, int32 CountY, int32 Sections, int32 Quads, double InitialHeight, bool bDryRun)
{
    FString Error;
    if (!EditorReady(Error)) { return Fail(Error, FString(), bDryRun); }
    ULevel* Level = FindObject<ULevel>(nullptr, *LevelPath);
    if (!Level || Level->GetPathName() != LevelPath || !LevelValid(Level,Error))
    { return Fail(Error.IsEmpty() ? TEXT("Level not found at exact path.") : Error, FString(), bDryRun); }
    if (Name.IsEmpty() || Name.Len() > 64 || !FChar::IsAlpha(Name[0]))
    { return Fail(TEXT("Landscape name must start with a letter and contain at most 64 letters/digits/underscores."), FString(), bDryRun); }
    for (TCHAR C : Name) if (!FChar::IsAlnum(C) && C != TCHAR('_')) { return Fail(TEXT("Invalid Landscape name."), FString(), bDryRun); }
    for (TActorIterator<AActor> It(Level->GetWorld()); It; ++It)
        if (It->GetName().Equals(Name, ESearchCase::IgnoreCase) || It->GetActorLabel().Equals(Name, ESearchCase::IgnoreCase))
        { return Fail(TEXT("Landscape name/label already exists; it will not be reused or renamed."), FString(), bDryRun); }
    if (FindObject<UObject>(Level, *Name)) { return Fail(TEXT("An object with that name already exists."), FString(), bDryRun); }
    if (!VectorValid(Location) || !VectorValid(Scale) || Scale.GetMin() < 1 || Scale.GetMax() > 1000 || !Coordinate(InitialHeight))
    { return Fail(TEXT("Non-finite / out-of-range Location, Scale or initial world height."), FString(), bDryRun); }
    if (CountX < 1 || CountX > 16 || CountY < 1 || CountY > 16 || CountX * CountY > 16
        || (Sections != 1 && Sections != 2) || (Quads != 7 && Quads != 15 && Quads != 31 && Quads != 63))
    { return Fail(TEXT("Invalid component layout: <=16 components, 1/2 sections, quads 7/15/31/63."), FString(), bDryRun); }
    const int32 SizeX = CountX*Sections*Quads+1, SizeY = CountY*Sections*Quads+1;
    uint16 Initial;
    if (int64(SizeX)*SizeY > MaxCreateSamples || (SizeX-1)*Scale.X > 100000 || (SizeY-1)*Scale.Y > 100000
        || !VectorValid(Location + FVector((SizeX-1)*Scale.X,(SizeY-1)*Scale.Y,0))
        || !Encode(InitialHeight,Location.Z,Scale.Z,Initial))
    { return Fail(TEXT("Creation exceeds sample/size/coordinate limits or representable height range."), FString(), bDryRun); }
    FLandscapeMCPResult R;
    R.bSuccess = true; R.bDryRun = bDryRun; R.SampleCount = SizeX*SizeY;
    R.LandscapePath = LevelPath + TEXT(".") + Name;
    R.HeightCm = Location.Z + (double(Initial)-32768)*Scale.Z/128;
    R.HeightQuantizationCm = Scale.Z/128;
    R.WorldMin = FVector(Location.X,Location.Y,R.HeightCm);
    R.WorldMax = FVector(Location.X+(SizeX-1)*Scale.X,Location.Y+(SizeY-1)*Scale.Y,R.HeightCm);
    R.Message = TEXT("Validated; no saving is performed.");
    if (bDryRun) { return R; }
    if (GEditor->IsTransactionActive()) { return Fail(TEXT("Nested write transactions are unsupported."),R.LandscapePath); }
    ALandscape* Actor = nullptr;
    {
        FScopedTransaction Transaction(INVTEXT("Landscape MCP Create"));
        Level->Modify();
        FActorSpawnParameters Params; Params.OverrideLevel = Level; Params.Name = FName(*Name);
        Params.ObjectFlags = RF_Transactional;
        Actor = Level->GetWorld()->SpawnActor<ALandscape>(Location, FRotator::ZeroRotator, Params);
        if (!Actor) { Transaction.Cancel(); return Fail(TEXT("Landscape spawn failed."),R.LandscapePath); }
        Actor->Modify(); Actor->SetActorLabel(Name); Actor->SetActorScale3D(Scale);
        TMap<FGuid,TArray<uint16>> HeightData;
        TArray<uint16> Data; Data.Init(Initial,R.SampleCount); HeightData.Add(FGuid(),MoveTemp(Data));
        TMap<FGuid,TArray<FLandscapeImportLayerInfo>> WeightData; WeightData.Add(FGuid(),{});
        Actor->Import(FGuid::NewGuid(),0,0,SizeX-1,SizeY-1,Sections,Quads,HeightData,TEXT(""),WeightData,
            ELandscapeImportAlphamapType::Additive,TArrayView<const FLandscapeLayer>());
        Actor->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Heightmap_All);
        Actor->ForceUpdateLayersContent();
        R.LandscapePath = Actor->GetPathName();
    }
    FTarget Target;
    if (!Resolve(R.LandscapePath,Target,Error) || Actor->LandscapeComponents.Num() != CountX*CountY)
    {
        const bool bUndone = GEditor->UndoTransaction(false);
        return Fail(FString(TEXT("Creation verification failed; rollback ")) + (bUndone ? TEXT("completed: ") : TEXT("FAILED: ")) + Error,R.LandscapePath);
    }
    const FLandscapeMCPResult Check = Height(R.LandscapePath,Location.X,Location.Y);
    if (!Check.bSuccess || FMath::Abs(Check.HeightCm-R.HeightCm) > R.HeightQuantizationCm)
    {
        const bool bUndone = GEditor->UndoTransaction(false);
        return Fail(FString(TEXT("Initial height verification failed; rollback ")) + (bUndone ? TEXT("completed.") : TEXT("FAILED.")),R.LandscapePath);
    }
    R.Message = TEXT("Landscape created and source height verified; Undo available; nothing saved.");
    return R;
}

bool Authoring::EncodeHeight(double WorldZ,double OriginZ,double ScaleZ,uint16& Out)
{ return Encode(WorldZ,OriginZ,ScaleZ,Out); }

bool Authoring::ReadSnapshot(const FString& Path,FVector2D Min,FVector2D Max,FHeightfieldSnapshot& Out,FString& Code,FString& Error)
{
    Out={};
    if(!Coordinate(Min.X)||!Coordinate(Min.Y)||!Coordinate(Max.X)||!Coordinate(Max.Y)||Min.X>Max.X||Min.Y>Max.Y)
    {Code=TEXT("INVALID_COORDINATE");Error=Code;return false;}
    FTarget T;
    if(!Resolve(Path,T,Error))
    {
        Code=Error.Contains(TEXT("foliage"))?TEXT("FOLIAGE_SAFETY_CONFLICT"):TEXT("UNSUPPORTED_LANDSCAPE_STATE");
        if(Error.Contains(TEXT("not found")))Code=TEXT("TARGET_NOT_FOUND");
        if(Error.Contains(TEXT("exact loaded")))Code=TEXT("INVALID_TARGET_PATH");
        return false;
    }
    const FVector2D LocalMin((Min.X-T.Origin.X)/T.Scale.X,(Min.Y-T.Origin.Y)/T.Scale.Y);
    const FVector2D LocalMax((Max.X-T.Origin.X)/T.Scale.X,(Max.Y-T.Origin.Y)/T.Scale.Y);
    const int32 X0=FMath::FloorToInt(LocalMin.X),Y0=FMath::FloorToInt(LocalMin.Y),X1=FMath::CeilToInt(LocalMax.X),Y1=FMath::CeilToInt(LocalMax.Y);
    if((int64(X1)-X0+1)*(int64(Y1)-Y0+1)>Authoring::MaxAabbCandidates)
    {Code=TEXT("SAMPLE_LIMIT_EXCEEDED");Error=TEXT("Unclipped AABB exceeds 65536 candidates.");return false;}
    if(X1<T.Extent.Min.X||Y1<T.Extent.Min.Y||X0>T.Extent.Max.X||Y0>T.Extent.Max.Y)
    {Code=TEXT("OUTSIDE_LANDSCAPE");Error=Code;return false;}
    const FIntRect Rect(FMath::Max(T.Extent.Min.X,X0-1),FMath::Max(T.Extent.Min.Y,Y0-1),FMath::Min(T.Extent.Max.X,X1+1),FMath::Min(T.Extent.Max.Y,Y1+1));
    const int64 Count=(int64(Rect.Max.X)-Rect.Min.X+1)*(int64(Rect.Max.Y)-Rect.Min.Y+1);
    if(Count>Authoring::MaxReadVertices)
    {Code=TEXT("SAMPLE_LIMIT_EXCEEDED");Error=TEXT("Read and verification halo exceeds 16384 vertices.");return false;}
    FSourceReader Reader(T,Authoring::MaxCachedSourceBytes);
    if(!Reader.Region(Rect,Out.Heights,Error))
    {Code=Error.StartsWith(TEXT("SAMPLE_LIMIT_EXCEEDED"))?TEXT("SAMPLE_LIMIT_EXCEEDED"):TEXT("UNSUPPORTED_SOURCE_DATA");return false;}
    Out.Origin=T.Origin;Out.Scale=T.Scale;Out.Extent=T.Extent;Out.Rect=Rect;Out.CachedSourceBytes=Reader.GetCachedBytes();return true;
}

FLandscapeMCPResult Height(const FString& Path, double X, double Y)
{
    if (!Coordinate(X) || !Coordinate(Y)) { return Fail(TEXT("Non-finite / out-of-range world XY."),Path); }
    FTarget T; FString Error;
    if (!Resolve(Path,T,Error)) { return Fail(Error,Path); }
    const FVector2D Local((X-T.Origin.X)/T.Scale.X,(Y-T.Origin.Y)/T.Scale.Y);
    if (!Contains(T.Extent,Local)) { return Fail(TEXT("Query point is outside Landscape."),Path); }
    const int32 X0 = FMath::FloorToInt(Local.X), Y0 = FMath::FloorToInt(Local.Y);
    const int32 X1 = FMath::Min(X0+1,T.Extent.Max.X), Y1 = FMath::Min(Y0+1,T.Extent.Max.Y);
    FSourceReader Reader(T); uint16 H00,H10,H01,H11;
    if (!Reader.Read(X0,Y0,H00,Error) || !Reader.Read(X1,Y0,H10,Error)
        || !Reader.Read(X0,Y1,H01,Error) || !Reader.Read(X1,Y1,H11,Error)) { return Fail(Error,Path); }
    const double H = FMath::Lerp(FMath::Lerp(double(H00),double(H10),Local.X-X0),
        FMath::Lerp(double(H01),double(H11),Local.X-X0),Local.Y-Y0);
    FLandscapeMCPResult R; R.bSuccess = true; R.LandscapePath = Path; R.SampleCount = 4;
    R.HeightCm = WorldHeight(T,H); R.HeightQuantizationCm = T.Scale.Z/128;
    R.WorldMin = R.WorldMax = FVector(X,Y,R.HeightCm);
    R.Message = TEXT("Read-only bilinear source height (not a collision raycast). No package or transaction changes.");
    return R;
}

FLandscapeMCPResult Edit(const FEditRequest& R)
{
    if (!Coordinate(R.Center.X) || !Coordinate(R.Center.Y) || !FMath::IsFinite(R.RadiusCm)
        || R.RadiusCm <= 0 || R.RadiusCm > MaxRadius || !FMath::IsFinite(R.Strength) || R.Strength < 0
        || R.Strength > (R.Operation == EOperation::Sculpt ? MaxSculpt : 1.0)
        || !FMath::IsFinite(R.Falloff) || R.Falloff < 0 || R.Falloff > 1
        || (R.Operation == EOperation::Flatten && !Coordinate(R.TargetHeightCm)))
    { return Fail(TEXT("Invalid Center / Radius / Strength / Falloff / TargetHeight (including NaN/Infinity)."),R.LandscapePath,R.bDryRun); }
    FTarget T; FString Error;
    if (!Resolve(R.LandscapePath,T,Error)) { return Fail(Error,R.LandscapePath,R.bDryRun); }
    const FVector2D Local((R.Center.X-T.Origin.X)/T.Scale.X,(R.Center.Y-T.Origin.Y)/T.Scale.Y);
    if (!Contains(T.Extent,Local)) { return Fail(TEXT("Brush centre is outside Landscape."),R.LandscapePath,R.bDryRun); }
    uint16 Dummy;
    if (R.Operation == EOperation::Flatten && !Encode(R.TargetHeightCm,T.Origin.Z,T.Scale.Z,Dummy))
    { return Fail(TEXT("Target height is outside the representable heightfield range."),R.LandscapePath,R.bDryRun); }
    const FIntRect Requested(FMath::FloorToInt(Local.X-R.RadiusCm/T.Scale.X),FMath::FloorToInt(Local.Y-R.RadiusCm/T.Scale.Y),
        FMath::CeilToInt(Local.X+R.RadiusCm/T.Scale.X),FMath::CeilToInt(Local.Y+R.RadiusCm/T.Scale.Y));
    const FIntRect Rect(FMath::Max(Requested.Min.X,T.Extent.Min.X),FMath::Max(Requested.Min.Y,T.Extent.Min.Y),
        FMath::Min(Requested.Max.X,T.Extent.Max.X),FMath::Min(Requested.Max.Y,T.Extent.Max.Y));
    const int32 Halo = R.Operation == EOperation::Smooth ? 1 : 0;
    const FIntRect ReadRect(FMath::Max(Rect.Min.X-Halo,T.Extent.Min.X),FMath::Max(Rect.Min.Y-Halo,T.Extent.Min.Y),
        FMath::Min(Rect.Max.X+Halo,T.Extent.Max.X),FMath::Min(Rect.Max.Y+Halo,T.Extent.Max.Y));
    const int64 ReadCount = int64(ReadRect.Width()+1)*(ReadRect.Height()+1);
    if (ReadCount > MaxEditSamples) { return Fail(TEXT("Edit/halo exceeds 16384 samples; reduce radius."),R.LandscapePath,R.bDryRun); }
    FSourceReader Reader(T); TArray<uint16> Original;
    if (!Reader.Region(ReadRect,Original,Error)) { return Fail(Error,R.LandscapePath,R.bDryRun); }
    const int32 Stride = ReadRect.Width()+1;
    auto At = [&](int32 X,int32 Y) -> uint16 { return Original[(Y-ReadRect.Min.Y)*Stride+X-ReadRect.Min.X]; };
    TArray<uint16> Output; Output.Reserve((Rect.Width()+1)*(Rect.Height()+1));
    FLandscapeMCPResult Result; Result.bSuccess = true; Result.bDryRun = R.bDryRun;
    Result.LandscapePath = R.LandscapePath; Result.bClipped = Requested != Rect;
    Result.HeightQuantizationCm = T.Scale.Z/128;
    double MinZ = TNumericLimits<double>::Max(), MaxZ = TNumericLimits<double>::Lowest();
    bool bFirstChange = true;
    for (int32 Y=Rect.Min.Y; Y<=Rect.Max.Y; ++Y)
    {
        for (int32 X=Rect.Min.X; X<=Rect.Max.X; ++X)
        {
            const uint16 Before = At(X,Y); uint16 After = Before;
            const FVector2D World(T.Origin.X+X*T.Scale.X,T.Origin.Y+Y*T.Scale.Y);
            const double Distance = FVector2D::Distance(World,R.Center);
            if (Distance <= R.RadiusCm)
            {
                ++Result.SampleCount;
                const double Weight = BrushWeight(Distance,R.RadiusCm,R.Falloff);
                const double Z = WorldHeight(T,Before); double NewZ = Z;
                if (R.Operation == EOperation::Sculpt) { NewZ += (R.bRaise ? 1 : -1)*R.Strength*Weight; }
                else if (R.Operation == EOperation::Flatten) { NewZ = FMath::Lerp(Z,R.TargetHeightCm,R.Strength*Weight); }
                else
                {
                    double Sum=0; int32 Count=0;
                    for (int32 NY=FMath::Max(Y-1,T.Extent.Min.Y); NY<=FMath::Min(Y+1,T.Extent.Max.Y); ++NY)
                        for (int32 NX=FMath::Max(X-1,T.Extent.Min.X); NX<=FMath::Min(X+1,T.Extent.Max.X); ++NX)
                        { Sum += WorldHeight(T,At(NX,NY)); ++Count; }
                    NewZ = FMath::Lerp(Z,Sum/Count,R.Strength*Weight);
                }
                if (!Encode(NewZ,T.Origin.Z,T.Scale.Z,After))
                { return Fail(TEXT("Entire edit rejected: at least one sample would overflow the heightfield."),R.LandscapePath,R.bDryRun); }
            }
            if (Before != After)
            {
                ++Result.ChangedSampleCount; const double Delta = WorldHeight(T,After)-WorldHeight(T,Before);
                Result.MinDeltaCm = bFirstChange ? Delta : FMath::Min(Result.MinDeltaCm,Delta);
                Result.MaxDeltaCm = bFirstChange ? Delta : FMath::Max(Result.MaxDeltaCm,Delta); bFirstChange = false;
            }
            MinZ = FMath::Min(MinZ,WorldHeight(T,After)); MaxZ = FMath::Max(MaxZ,WorldHeight(T,After));
            Output.Add(After);
        }
    }
    if (Result.SampleCount == 0) { return Fail(TEXT("Brush contains no heightfield vertices."),R.LandscapePath,R.bDryRun); }
    Result.WorldMin = FVector(T.Origin.X+Rect.Min.X*T.Scale.X,T.Origin.Y+Rect.Min.Y*T.Scale.Y,MinZ);
    Result.WorldMax = FVector(T.Origin.X+Rect.Max.X*T.Scale.X,T.Origin.Y+Rect.Max.Y*T.Scale.Y,MaxZ);
    Result.Message = TEXT("Validated complete edit; deltas include heightfield quantization; nothing saved.");
    if (R.bDryRun || Result.ChangedSampleCount == 0) { return Result; }
    if (GEditor->IsTransactionActive()) { return Fail(TEXT("Nested write transactions are unsupported."),R.LandscapePath); }
    {
        FScopedTransaction Transaction(INVTEXT("Landscape MCP Heightfield Edit"));
        T.Actor->Modify(); T.Info->Modify();
        {
            FScopedSetLandscapeEditingLayer LayerScope(T.Actor,T.LayerGuid);
            FLandscapeEditDataInterface EditData(T.Info,T.LayerGuid);
            EditData.SetHeightData(Rect.Min.X,Rect.Min.Y,Rect.Max.X,Rect.Max.Y,Output.GetData(),0,false,
                nullptr,nullptr,nullptr,false,nullptr,nullptr,true,true,true);
            EditData.Flush();
        }
        T.Actor->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Heightmap_All);
        T.Actor->ForceUpdateLayersContent();
    }
    FSourceReader Verify(T); TArray<uint16> Actual;
    if (!Verify.Region(Rect,Actual,Error) || Actual != Output)
    {
        const bool bUndone = GEditor->UndoTransaction(false);
        return Fail(FString(TEXT("Source verification failed; rollback ")) + (bUndone ? TEXT("completed.") : TEXT("FAILED; stop editing.")),R.LandscapePath);
    }
    Result.Message = TEXT("Source heightfield verified after edit; layer/collision update requested; Undo available; nothing saved.");
    return Result;
}

namespace
{
    constexpr double MinSampleDistance = 1.0;
    constexpr double MaxSampleDistance = 5000.0;
    constexpr int32 MaxRegionSamples = 1024;

    // Heightと同じbilinear補間。解決済みの対象とReaderを共有し、複数点を1回のValidationで読む。
    bool SampleHeight(const FTarget& T, FSourceReader& Reader, double X, double Y, double& OutZ, bool& bOutside, FString& Error)
    {
        const FVector2D Local((X-T.Origin.X)/T.Scale.X,(Y-T.Origin.Y)/T.Scale.Y);
        bOutside = !Contains(T.Extent,Local);
        if (bOutside) { Error = TEXT("Sample point is outside Landscape."); return false; }
        const int32 X0 = FMath::FloorToInt(Local.X), Y0 = FMath::FloorToInt(Local.Y);
        const int32 X1 = FMath::Min(X0+1,T.Extent.Max.X), Y1 = FMath::Min(Y0+1,T.Extent.Max.Y);
        uint16 H00,H10,H01,H11;
        if (!Reader.Read(X0,Y0,H00,Error) || !Reader.Read(X1,Y0,H10,Error)
            || !Reader.Read(X0,Y1,H01,Error) || !Reader.Read(X1,Y1,H11,Error)) { return false; }
        OutZ = WorldHeight(T,FMath::Lerp(FMath::Lerp(double(H00),double(H10),Local.X-X0),
            FMath::Lerp(double(H01),double(H11),Local.X-X0),Local.Y-Y0));
        return true;
    }
    struct FSlopeMeasurement
    {
        Analysis::FSlope Slope;
        double HeightCenterCm = 0;
        double QuantizationCm = 0;
        double UncertaintyDegrees = 0;
    };
    // GetSlopeとEvaluateWalkabilityが同じ定義を使うための共通経路。
    bool SlopeInputValid(double X, double Y, double D, FString& Error)
    {
        if (!Coordinate(X) || !Coordinate(Y)) { Error = TEXT("Non-finite / out-of-range world XY."); return false; }
        if (!FMath::IsFinite(D) || D < MinSampleDistance || D > MaxSampleDistance)
        { Error = TEXT("SampleDistanceCm must be finite and within [1,5000]."); return false; }
        return true;
    }
    bool MeasureSlopeAt(const FTarget& T, FSourceReader& Reader, double X, double Y, double D, FSlopeMeasurement& Out, FString& Error)
    {
        bool bOutside = false;
        if (!SampleHeight(T,Reader,X,Y,Out.HeightCenterCm,bOutside,Error))
        { if (bOutside) { Error = TEXT("Query point is outside Landscape."); } return false; }
        double XMinus = 0, XPlus = 0, YMinus = 0, YPlus = 0;
        // 端で片側差分へ切り替えると定義が変わるため、4点が揃わない場合は計測しない。
        if (!SampleHeight(T,Reader,X-D,Y,XMinus,bOutside,Error) || !SampleHeight(T,Reader,X+D,Y,XPlus,bOutside,Error)
            || !SampleHeight(T,Reader,X,Y-D,YMinus,bOutside,Error) || !SampleHeight(T,Reader,X,Y+D,YPlus,bOutside,Error))
        {
            if (bOutside) { Error = TEXT("Slope stencil reaches outside Landscape; move the point inward or reduce SampleDistanceCm."); }
            return false;
        }
        Out.Slope = Analysis::SlopeFromHeights(XMinus,XPlus,YMinus,YPlus,D);
        Out.QuantizationCm = T.Scale.Z/128;
        Out.UncertaintyDegrees = Analysis::SlopeUncertaintyDegrees(Out.Slope,Out.QuantizationCm,D);
        if (!FMath::IsFinite(Out.Slope.SlopeDegrees) || !FMath::IsFinite(Out.Slope.DirectionDegrees))
        { Error = TEXT("Slope evaluation produced a non-finite value."); return false; }
        return true;
    }
    bool MeasureSlope(const FString& Path, double X, double Y, double D, FSlopeMeasurement& Out, FString& Error)
    {
        if (!SlopeInputValid(X,Y,D,Error)) { return false; }
        FTarget T;
        if (!Resolve(Path,T,Error)) { return false; }
        FSourceReader Reader(T);
        return MeasureSlopeAt(T,Reader,X,Y,D,Out,Error);
    }
    constexpr int32 MaxAnalysisVertices = MaxEditSamples;
    // 実三角形の評価に使う頂点格子。Rectは頂点座標で最大値を含む。Heightsは行優先のワールドZ。
    struct FTriangleGrid
    {
        FIntRect Rect;
        int32 CountX = 0;
        int32 CountY = 0;
        TArray<double> Heights;
    };
    // Landscape格子座標の矩形に重なるセルを覆う頂点格子を読む。範囲外はLandscape内へ収め、各軸1セル以上にする。
    bool ReadTriangleGrid(const FTarget& T, FSourceReader& Reader, FVector2D LocalMin, FVector2D LocalMax, int32 MaxVertices,
        FTriangleGrid& Out, FString& Error)
    {
        FIntRect Rect;
        Rect.Min.X = FMath::Clamp(FMath::FloorToInt32(LocalMin.X),T.Extent.Min.X,T.Extent.Max.X-1);
        Rect.Min.Y = FMath::Clamp(FMath::FloorToInt32(LocalMin.Y),T.Extent.Min.Y,T.Extent.Max.Y-1);
        Rect.Max.X = FMath::Clamp(FMath::CeilToInt32(LocalMax.X),Rect.Min.X+1,T.Extent.Max.X);
        Rect.Max.Y = FMath::Clamp(FMath::CeilToInt32(LocalMax.Y),Rect.Min.Y+1,T.Extent.Max.Y);
        const int64 CountX = int64(Rect.Max.X)-Rect.Min.X+1, CountY = int64(Rect.Max.Y)-Rect.Min.Y+1;
        if (CountX*CountY > MaxVertices)
        { Error = TEXT("Analysis exceeds 16384 heightfield vertices; reduce the radius or split the region."); return false; }
        TArray<uint16> Raw;
        if (!Reader.Region(Rect,Raw,Error)) { return false; }
        Out.Rect = Rect; Out.CountX = int32(CountX); Out.CountY = int32(CountY);
        Out.Heights.SetNumUninitialized(Raw.Num());
        for (int32 Index = 0; Index < Raw.Num(); ++Index) { Out.Heights[Index] = WorldHeight(T,Raw[Index]); }
        return true;
    }
    // 三角形の重心のワールド位置。Zは3頂点の平均高度。
    FVector TriangleLocation(const FTarget& T, const FTriangleGrid& Grid, int32 CellX, int32 CellY, bool bUpper)
    {
        const FVector2D Centroid = Analysis::TriangleCentroid(Grid.Rect.Min.X+CellX,Grid.Rect.Min.Y+CellY,bUpper);
        const int32 Base = CellY*Grid.CountX + CellX;
        const double Third = bUpper ? Grid.Heights[Base+Grid.CountX] : Grid.Heights[Base+1];
        return FVector(T.Origin.X+Centroid.X*T.Scale.X, T.Origin.Y+Centroid.Y*T.Scale.Y,
            (Grid.Heights[Base]+Grid.Heights[Base+Grid.CountX+1]+Third)/3.0);
    }
}

FLandscapeMCPSlopeResult Slope(const FString& Path, double X, double Y, double SampleDistanceCm)
{
    FLandscapeMCPSlopeResult R; R.LandscapePath = Path; R.WorldX = X; R.WorldY = Y; R.SampleDistanceCm = SampleDistanceCm;
    FSlopeMeasurement M; FString Error;
    if (!MeasureSlope(Path,X,Y,SampleDistanceCm,M,Error)) { R.Message = Error; return R; }
    R.bSuccess = true;
    R.SlopeDegrees = M.Slope.SlopeDegrees; R.SlopeDirectionDegrees = M.Slope.DirectionDegrees;
    R.bDirectionValid = M.Slope.bDirectionValid; R.Normal = M.Slope.Normal;
    R.HeightCenterCm = M.HeightCenterCm; R.HeightQuantizationCm = M.QuantizationCm;
    R.SlopeUncertaintyDegrees = M.UncertaintyDegrees;
    R.Message = TEXT("Read-only central-difference slope of the bilinear source heightfield (not a collision normal). No package or transaction changes.");
    return R;
}

FLandscapeMCPWalkabilityResult Walkability(const FString& Path, double X, double Y, double WalkableFloorAngleDeg, double SampleDistanceCm)
{
    FLandscapeMCPWalkabilityResult R; R.LandscapePath = Path; R.WorldX = X; R.WorldY = Y;
    R.WalkableFloorAngleDeg = WalkableFloorAngleDeg; R.SampleDistanceCm = SampleDistanceCm;
    if (!FMath::IsFinite(WalkableFloorAngleDeg) || WalkableFloorAngleDeg < 0 || WalkableFloorAngleDeg > 90)
    { R.Message = TEXT("WalkableFloorAngleDeg must be finite and within [0,90]."); return R; }
    FSlopeMeasurement M; FString Error;
    if (!MeasureSlope(Path,X,Y,SampleDistanceCm,M,Error)) { R.Message = Error; return R; }
    const Analysis::FWalkability W = Analysis::EvaluateWalkability(M.Slope.SlopeDegrees,WalkableFloorAngleDeg,M.UncertaintyDegrees);
    R.bSuccess = true;
    R.SlopeDegrees = M.Slope.SlopeDegrees; R.SlopeDirectionDegrees = M.Slope.DirectionDegrees;
    R.bDirectionValid = M.Slope.bDirectionValid; R.HeightCenterCm = M.HeightCenterCm;
    R.SlopeUncertaintyDegrees = M.UncertaintyDegrees;
    R.bWalkable = W.bWalkable; R.MarginDegrees = W.MarginDegrees; R.Classification = Analysis::ToString(W.Classification);
    R.Message = TEXT("Read-only geometry evaluation against the caller-supplied angle; CharacterMovement is not consulted. No package or transaction changes.");
    return R;
}

FLandscapeMCPHeightRegionResult HeightRegion(const FString& Path, double MinX, double MinY, double MaxX, double MaxY, double SpacingCm)
{
    FLandscapeMCPHeightRegionResult R; R.LandscapePath = Path; R.SampleSpacingCm = SpacingCm;
    if (!Coordinate(MinX) || !Coordinate(MinY) || !Coordinate(MaxX) || !Coordinate(MaxY))
    { R.Message = TEXT("Non-finite / out-of-range region bounds."); return R; }
    if (MaxX < MinX || MaxY < MinY) { R.Message = TEXT("Reversed region bounds: max must be >= min."); return R; }
    if (!FMath::IsFinite(SpacingCm) || SpacingCm < MinSampleDistance || SpacingCm > MaxSampleDistance)
    { R.Message = TEXT("SampleSpacingCm must be finite and within [1,5000]."); return R; }
    // 割り切れる入力が浮動小数点誤差で1サンプル減らないよう、わずかな余裕を持たせる。
    const int64 CountX = int64(FMath::FloorToDouble((MaxX-MinX)/SpacingCm + 1.e-9)) + 1;
    const int64 CountY = int64(FMath::FloorToDouble((MaxY-MinY)/SpacingCm + 1.e-9)) + 1;
    if (CountX > MaxRegionSamples || CountY > MaxRegionSamples || CountX*CountY > MaxRegionSamples)
    { R.Message = TEXT("Region exceeds 1024 samples; increase SampleSpacingCm or split the region."); return R; }
    FTarget T; FString Error;
    if (!Resolve(Path,T,Error)) { R.Message = Error; return R; }
    const FVector2D LocalMin((MinX-T.Origin.X)/T.Scale.X,(MinY-T.Origin.Y)/T.Scale.Y);
    const FVector2D LocalMax((MaxX-T.Origin.X)/T.Scale.X,(MaxY-T.Origin.Y)/T.Scale.Y);
    if (!Contains(T.Extent,LocalMin) || !Contains(T.Extent,LocalMax))
    { R.Message = TEXT("Region is not fully inside Landscape; it is not clipped."); return R; }
    const int32 NX = int32(CountX), NY = int32(CountY);
    FSourceReader Reader(T); bool bOutside = false;
    TArray<double> Heights; Heights.Reserve(NX*NY);
    for (int32 IY = 0; IY < NY; ++IY)
    {
        for (int32 IX = 0; IX < NX; ++IX)
        {
            // 余裕分でmaxをわずかに超えた場合も、検証済みの矩形内へ収める。
            const double X = FMath::Min(MinX + IX*SpacingCm, MaxX), Y = FMath::Min(MinY + IY*SpacingCm, MaxY);
            double Z = 0;
            if (!SampleHeight(T,Reader,X,Y,Z,bOutside,Error)) { R.Message = Error; return R; }
            Heights.Add(Z);
        }
    }
    const Analysis::FRegionStats S = Analysis::ComputeRegionStats(Heights,NX,NY,SpacingCm);
    if (S.MinIndex == INDEX_NONE || !FMath::IsFinite(S.MeanHeight)) { R.Message = TEXT("Region statistics could not be evaluated."); return R; }
    auto Location = [&](int32 Index) { return FVector(MinX + (Index%NX)*SpacingCm, MinY + (Index/NX)*SpacingCm, Heights[Index]); };
    R.bSuccess = true;
    R.SampleCount = Heights.Num(); R.SampleCountX = NX; R.SampleCountY = NY;
    R.MinHeightCm = S.MinHeight; R.MaxHeightCm = S.MaxHeight; R.MeanHeightCm = S.MeanHeight;
    R.MinHeightLocation = Location(S.MinIndex); R.MaxHeightLocation = Location(S.MaxIndex);
    R.WorldMin = FVector(MinX,MinY,S.MinHeight);
    R.WorldMax = FVector(MinX + (NX-1)*SpacingCm, MinY + (NY-1)*SpacingCm, S.MaxHeight);
    R.bSlopeValid = S.bSlopeValid;
    if (S.bSlopeValid)
    {
        const int32 Base = S.MaxSlopeCellY*NX + S.MaxSlopeCellX;
        R.MaxSlopeDegrees = S.MaxSlopeDegrees;
        R.MaxSlopeLocation = FVector(MinX + (S.MaxSlopeCellX+0.5)*SpacingCm, MinY + (S.MaxSlopeCellY+0.5)*SpacingCm,
            (Heights[Base]+Heights[Base+1]+Heights[Base+NX]+Heights[Base+NX+1])/4.0);
    }
    R.HeightQuantizationCm = T.Scale.Z/128;
    {
        FTriangleGrid Grid;
        if (!ReadTriangleGrid(T,Reader,LocalMin,LocalMax,TNumericLimits<int32>::Max(),Grid,Error))
        { FLandscapeMCPHeightRegionResult Failed; Failed.LandscapePath = Path; Failed.SampleSpacingCm = SpacingCm; Failed.Message = Error; return Failed; }
        const Analysis::FTriangleStats Triangles = Analysis::ComputeTriangleStats(Grid.Heights,Grid.CountX,Grid.CountY,T.Scale.X,T.Scale.Y,
            90.0,0.0,[](int32,int32,bool) { return true; });
        R.bTriangleSlopeValid = Triangles.TriangleCount > 0;
        R.TriangleCount = Triangles.TriangleCount;
        if (R.bTriangleSlopeValid)
        {
            R.MaxTriangleSlopeDegrees = Triangles.MaxSlopeDegrees;
            R.MaxTriangleSlopeLocation = TriangleLocation(T,Grid,Triangles.MaxCellX,Triangles.MaxCellY,Triangles.bMaxIsUpper);
        }
    }
    R.HeightsCm = MoveTemp(Heights);
    R.Message = TEXT("Read-only bilinear source heights on a regular grid (row-major, Y outer). No package or transaction changes.");
    return R;
}

FLandscapeMCPSlopeNeighborhoodResult SlopeNeighborhood(const FString& Path, double X, double Y, double RadiusCm, double SampleDistanceCm)
{
    FLandscapeMCPSlopeNeighborhoodResult R; R.LandscapePath = Path; R.WorldX = X; R.WorldY = Y;
    R.RadiusCm = RadiusCm; R.SampleDistanceCm = SampleDistanceCm;
    FString Error;
    if (!SlopeInputValid(X,Y,SampleDistanceCm,Error)) { R.Message = Error; return R; }
    if (!FMath::IsFinite(RadiusCm) || RadiusCm <= 0 || RadiusCm > MaxRadius)
    { R.Message = TEXT("RadiusCm must be finite and within (0,5000]."); return R; }
    FTarget T;
    if (!Resolve(Path,T,Error)) { R.Message = Error; return R; }
    FSourceReader Reader(T); FSlopeMeasurement M;
    // 中心傾斜はGetSlopeと同じ経路で測る。端で4点が揃わない場合は全体を拒否する。
    if (!MeasureSlopeAt(T,Reader,X,Y,SampleDistanceCm,M,Error)) { R.Message = Error; return R; }
    const FVector2D Local((X-T.Origin.X)/T.Scale.X,(Y-T.Origin.Y)/T.Scale.Y);
    const FVector2D Reach(RadiusCm/T.Scale.X,RadiusCm/T.Scale.Y);
    const FVector2D RequestMin = Local-Reach, RequestMax = Local+Reach;
    FTriangleGrid Grid;
    if (!ReadTriangleGrid(T,Reader,RequestMin,RequestMax,MaxAnalysisVertices,Grid,Error)) { R.Message = Error; return R; }
    const int32 CenterCellX = FMath::Clamp(FMath::FloorToInt32(Local.X),T.Extent.Min.X,T.Extent.Max.X-1) - Grid.Rect.Min.X;
    const int32 CenterCellY = FMath::Clamp(FMath::FloorToInt32(Local.Y),T.Extent.Min.Y,T.Extent.Max.Y-1) - Grid.Rect.Min.Y;
    const double RadiusSquared = RadiusCm*RadiusCm;
    const Analysis::FTriangleStats S = Analysis::ComputeTriangleStats(Grid.Heights,Grid.CountX,Grid.CountY,T.Scale.X,T.Scale.Y,
        90.0,T.Scale.Z/128,[&](int32 CellX,int32 CellY,bool bUpper)
        {
            // 半径が1セルより小さくても、中心を含むセルの2枚は必ず評価する。
            if (CellX == CenterCellX && CellY == CenterCellY) { return true; }
            const FVector2D Centroid = Analysis::TriangleCentroid(Grid.Rect.Min.X+CellX,Grid.Rect.Min.Y+CellY,bUpper);
            const double DX = T.Origin.X+Centroid.X*T.Scale.X-X, DY = T.Origin.Y+Centroid.Y*T.Scale.Y-Y;
            return DX*DX+DY*DY <= RadiusSquared;
        });
    if (S.TriangleCount == 0 || !FMath::IsFinite(S.MaxSlopeDegrees) || !FMath::IsFinite(S.MeanSlopeDegrees))
    { R.Message = TEXT("Neighborhood slope could not be evaluated."); return R; }
    R.bSuccess = true;
    R.CenterSlopeDegrees = M.Slope.SlopeDegrees; R.CenterSlopeDirectionDegrees = M.Slope.DirectionDegrees;
    R.bCenterDirectionValid = M.Slope.bDirectionValid; R.CenterSlopeUncertaintyDegrees = M.UncertaintyDegrees;
    R.HeightCenterCm = M.HeightCenterCm; R.HeightQuantizationCm = M.QuantizationCm;
    R.LocalMaxSlopeDegrees = S.MaxSlopeDegrees; R.LocalMeanSlopeDegrees = S.MeanSlopeDegrees;
    R.MaxSlopeLocation = TriangleLocation(T,Grid,S.MaxCellX,S.MaxCellY,S.bMaxIsUpper);
    R.MaxSlopeDirectionDegrees = S.MaxSlope.DirectionDegrees; R.bMaxSlopeDirectionValid = S.MaxSlope.bDirectionValid;
    R.LocalMaxSlopeUncertaintyDegrees = S.MaxSlopeUncertaintyDegrees;
    R.TriangleCount = S.TriangleCount; R.SampleCount = Grid.Heights.Num();
    R.bClipped = RequestMin.X < T.Extent.Min.X || RequestMin.Y < T.Extent.Min.Y
        || RequestMax.X > T.Extent.Max.X || RequestMax.Y > T.Extent.Max.Y;
    R.Message = TEXT("Read-only: centre slope is the GetSlope central difference; local slopes come from the Landscape triangles (diagonal 00-11) within the radius. No package or transaction changes.");
    return R;
}

FLandscapeMCPWalkabilityRegionResult WalkabilityRegion(const FString& Path, double MinX, double MinY, double MaxX, double MaxY, double WalkableFloorAngleDeg)
{
    FLandscapeMCPWalkabilityRegionResult R; R.LandscapePath = Path; R.WalkableFloorAngleDeg = WalkableFloorAngleDeg;
    if (!Coordinate(MinX) || !Coordinate(MinY) || !Coordinate(MaxX) || !Coordinate(MaxY))
    { R.Message = TEXT("Non-finite / out-of-range region bounds."); return R; }
    if (MaxX < MinX || MaxY < MinY) { R.Message = TEXT("Reversed region bounds: max must be >= min."); return R; }
    if (!FMath::IsFinite(WalkableFloorAngleDeg) || WalkableFloorAngleDeg < 0 || WalkableFloorAngleDeg > 90)
    { R.Message = TEXT("WalkableFloorAngleDeg must be finite and within [0,90]."); return R; }
    FTarget T; FString Error;
    if (!Resolve(Path,T,Error)) { R.Message = Error; return R; }
    const FVector2D LocalMin((MinX-T.Origin.X)/T.Scale.X,(MinY-T.Origin.Y)/T.Scale.Y);
    const FVector2D LocalMax((MaxX-T.Origin.X)/T.Scale.X,(MaxY-T.Origin.Y)/T.Scale.Y);
    if (!Contains(T.Extent,LocalMin) || !Contains(T.Extent,LocalMax))
    { R.Message = TEXT("Region is not fully inside Landscape; it is not clipped."); return R; }
    FSourceReader Reader(T); FTriangleGrid Grid;
    if (!ReadTriangleGrid(T,Reader,LocalMin,LocalMax,MaxAnalysisVertices,Grid,Error)) { R.Message = Error; return R; }
    const Analysis::FTriangleStats S = Analysis::ComputeTriangleStats(Grid.Heights,Grid.CountX,Grid.CountY,T.Scale.X,T.Scale.Y,
        WalkableFloorAngleDeg,T.Scale.Z/128,[](int32,int32,bool) { return true; });
    if (S.TriangleCount == 0 || !FMath::IsFinite(S.MaxSlopeDegrees))
    { R.Message = TEXT("Region walkability could not be evaluated."); return R; }
    double MinZ = Grid.Heights[0], MaxZ = Grid.Heights[0];
    for (double H : Grid.Heights) { MinZ = FMath::Min(MinZ,H); MaxZ = FMath::Max(MaxZ,H); }
    R.bSuccess = true;
    R.SampleCount = Grid.Heights.Num(); R.TriangleCount = S.TriangleCount;
    R.WalkableCount = S.WalkableCount; R.NearLimitCount = S.NearLimitCount; R.UnwalkableCount = S.UnwalkableCount;
    R.WalkableRatio = double(S.WalkableCount)/S.TriangleCount;
    R.MaxSlopeDegrees = S.MaxSlopeDegrees; R.WorstMarginDegrees = WalkableFloorAngleDeg - S.MaxSlopeDegrees;
    R.WorstLocation = TriangleLocation(T,Grid,S.MaxCellX,S.MaxCellY,S.bMaxIsUpper);
    R.WorstSlopeDirectionDegrees = S.MaxSlope.DirectionDegrees; R.bWorstSlopeDirectionValid = S.MaxSlope.bDirectionValid;
    R.SlopeUncertaintyDegrees = S.MaxSlopeUncertaintyDegrees;
    R.Classification = Analysis::ToString(Analysis::ClassifyRegion(S));
    R.WorldMin = FVector(T.Origin.X+Grid.Rect.Min.X*T.Scale.X,T.Origin.Y+Grid.Rect.Min.Y*T.Scale.Y,MinZ);
    R.WorldMax = FVector(T.Origin.X+Grid.Rect.Max.X*T.Scale.X,T.Origin.Y+Grid.Rect.Max.Y*T.Scale.Y,MaxZ);
    R.HeightQuantizationCm = T.Scale.Z/128;
    R.Message = TEXT("Read-only geometry evaluation of the Landscape triangles (diagonal 00-11) against the caller-supplied angle; CharacterMovement is not consulted. No package or transaction changes.");
    return R;
}
}

namespace LandscapeMCP::Authoring
{
namespace
{
    // Module lifetime, game-thread only. Never reset by a production request.
    bool bMutationDisabled=false;
#if WITH_DEV_AUTOMATION_TESTS
    EApplyFault AutomationFault=EApplyFault::None;
    bool Fault(EApplyFault F){return GIsAutomationTesting&&AutomationFault==F;}
#else
    enum class EApplyFault { None,SourceMismatch,FreshReadFailure,GeometryMismatch,CollisionFailure,UndoFalse,RestoreMismatch,
        RestoreCollisionFailure,OwnershipMismatch,InsertUnrelatedTransaction,StaleSource,RestoreCodecMismatch,RestoreCollisionStale };
    bool Fault(EApplyFault){return false;}
#endif
    struct FIdentity
    {
        TArray<TWeakObjectPtr<UObject>> Objects;
        FString State;
        FTransform Transform;
        uint32 LayerHash=0;
        bool operator==(const FIdentity& B)const{return Objects==B.Objects&&State==B.State&&Transform.Equals(B.Transform,0)&&LayerHash==B.LayerHash;}
    };
    struct FCollateral
    {
        TArray<TWeakObjectPtr<UObject>> Objects;
        TArray<uint32> Hashes;
        TArray<FString> Paths;
        bool operator==(const FCollateral& B)const{return Objects==B.Objects&&Hashes==B.Hashes&&Paths==B.Paths;}
    };
    struct FRetryRecord
    {
        FPreviewRequest Request;FIdentity Identity;FHeightfieldSnapshot ResultSnapshot;
        FPreparedTerrainPlan Plan;FLandscapeMCPTerrainOperationResult Result;
    };
    // Bounded idempotency cache: one record per request, oldest first, the oldest
    // is evicted at capacity. It only answers "this exact request was already
    // applied and the terrain still equals its verified result" (NO_CHANGE).
    // A missing, evicted or stale record is never a reason to accept or refuse:
    // the request is then validated, planned and verified from fresh state.
    constexpr int32 MaxRetryRecords=128;
    TArray<FRetryRecord> RetryRecords;
    bool SameRequest(const FPreviewRequest& A,const FPreviewRequest& B)
    {
        return A.Operation==B.Operation&&A.LandscapePath==B.LandscapePath&&A.Points==B.Points&&A.Center==B.Center
            &&A.SizeX==B.SizeX&&A.SizeY==B.SizeY&&A.Width==B.Width&&A.Falloff==B.Falloff&&A.MaxDelta==B.MaxDelta
            &&A.HeightMode==B.HeightMode&&A.TargetHeight==B.TargetHeight&&A.MaxSlope==B.MaxSlope&&A.Yaw==B.Yaw;
    }
    void RememberRetry(FRetryRecord&& Record)
    {
        RetryRecords.RemoveAll([&](const FRetryRecord& Old){return SameRequest(Old.Request,Record.Request);});
        while(RetryRecords.Num()>=MaxRetryRecords)RetryRecords.RemoveAt(0);
        RetryRecords.Add(MoveTemp(Record));
    }
    bool GrassSafe(const FTarget& T,FString& Error)
    {
        // Deliberately narrow allowlist. Material Instances, custom materials,
        // LOD overrides and grass caches are refused, including unknown graphs.
        UMaterial* Default=UMaterial::GetDefaultMaterial(MD_Surface);
        if(!Default||!Default->GetEditorOnlyData()||T.Actor->GetLandscapeMaterial()!=Default
            ||!T.Actor->GetPerLODOverrideMaterials().IsEmpty()||!T.Actor->GetGrassTypesOverrides().IsEmpty()
            ||!T.Actor->FoliageComponents.IsEmpty()||!T.Actor->AsyncFoliageTasks.IsEmpty())
        {Error=TEXT("Only the inspectable engine default material without grass state is supported for Apply.");return false;}
        TArray<UMaterialExpressionLandscapeGrassOutput*> Grass;
        Default->GetAllExpressionsInMaterialAndFunctionsOfType(Grass);
        if(!Grass.IsEmpty()){Error=TEXT("Landscape Grass Output is present.");return false;}
        for(ULandscapeComponent* C:T.Actor->LandscapeComponents)
            if(C->GetLandscapeMaterial()!=Default||!C->GetPerLODOverrideMaterials().IsEmpty()||!C->GetNamedGrassTypes().IsEmpty())
            {Error=TEXT("Component material override or grass state is unsupported.");return false;}
        return true;
    }
    bool CaptureTarget(const FString& Path,FTarget& T,FIdentity& I,FString& Code,FString& Error)
    {
        I={};if(!Resolve(Path,T,Error)){Code=Error.Contains(TEXT("foliage"))?TEXT("FOLIAGE_SAFETY_CONFLICT"):TEXT("UNSUPPORTED_LANDSCAPE_STATE");return false;}
        if(!GEditor->Trans||GEditor->IsTransactionActive()||GIsTransacting){Code=TEXT("ACTIVE_TRANSACTION");Error=TEXT("An idle Editor transaction buffer is required.");return false;}
        const auto* SavePolicy=GetDefault<UEditorLoadingSavingSettings>();
        if(!SavePolicy||SavePolicy->bAutoSaveEnable||SavePolicy->GetAutomaticallyCheckoutOnAssetModification()
            ||ISourceControlModule::Get().GetProvider().IsEnabled())
        {Code=TEXT("UNSUPPORTED_EDITOR_SAVE_POLICY");Error=TEXT("Apply requires autosave and automatic checkout disabled, and an inactive source control provider; no Editor settings are changed by this tool.");return false;}
        if(!GrassSafe(T,Error)){Code=TEXT("UNSUPPORTED_GRASS_STATE");return false;}
        if(T.Actor->CollisionMipLevel!=0||T.Actor->SimpleCollisionMipLevel!=0||T.Actor->bBakeMaterialPositionOffsetIntoCollision)
        {Code=TEXT("UNSUPPORTED_COLLISION_STATE");Error=TEXT("Native mip0 collision without material offsets is required.");return false;}
        I.Objects={T.Actor,T.Info,T.Actor->GetEditLayer(0),T.Actor->GetLevel(),T.Actor->GetLandscapeMaterial()};
        I.Transform=T.Actor->GetActorTransform();
        TArray<uint8> LayerBytes;FObjectWriter LayerWriter(T.Actor->GetEditLayer(0),LayerBytes);I.LayerHash=FCrc::MemCrc32(LayerBytes.GetData(),LayerBytes.Num());
        I.State=Path+TEXT("|")+T.Actor->GetLandscapeGuid().ToString()+TEXT("|")+T.Actor->GetActorTransform().ToString()+TEXT("|")+T.LayerGuid.ToString()
            +FString::Printf(TEXT("|%d,%d,%d,%d"),T.Extent.Min.X,T.Extent.Min.Y,T.Extent.Max.X,T.Extent.Max.Y);
        for(ULandscapeComponent* C:T.Actor->LandscapeComponents)
        {
            if(C->CollisionMipLevel!=0||C->SimpleCollisionMipLevel!=0||!C->GetCollisionComponent())
            {Code=TEXT("UNSUPPORTED_COLLISION_STATE");Error=TEXT("Every component requires native mip0 collision.");return false;}
            I.Objects.Add(C);I.Objects.Add(C->GetCollisionComponent());I.Objects.Add(C->GetHeightmap(T.LayerGuid));I.Objects.Add(C->GetHeightmap());
            I.State+=FString::Printf(TEXT("|%d,%d,%d,%d,%d|%s"),C->GetSectionBase().X,C->GetSectionBase().Y,C->ComponentSizeQuads,C->SubsectionSizeQuads,C->NumSubsections,*C->HeightmapScaleBias.ToString());
        }
        for(const auto& O:I.Objects)
        {
            if(!O.IsValid()){Code=TEXT("UNSUPPORTED_LANDSCAPE_STATE");Error=TEXT("Missing target identity.");return false;}
            I.State+=TEXT("|")+O->GetPathName();
        }
        return true;
    }
    bool CaptureCollateral(const FTarget& T,FCollateral& Out,FString& Error)
    {
        Out={};
        for(ULevel* L:T.Actor->GetWorld()->GetLevels())
        {
            Out.Objects.Add(L);Out.Paths.Add(L->GetPathName());Out.Hashes.Add(L->Actors.Num());
            for(AActor* A:L->Actors)
            {
                if(!A){Out.Objects.Add(nullptr);Out.Paths.Add(TEXT("null"));Out.Hashes.Add(0);continue;}
                Out.Objects.Add(A);Out.Paths.Add(A->GetPathName());
                if(A==T.Actor){Out.Hashes.Add(0);continue;}
                TArray<uint8> Bytes;FObjectWriter Writer(A,Bytes);Out.Hashes.Add(FCrc::MemCrc32(Bytes.GetData(),Bytes.Num()));
            }
        }
        TSet<UTexture2D*> Seen;
        for(ULandscapeComponent* C:T.Actor->LandscapeComponents)
        {
            TArray<UTexture2D*> Textures;
            for(UTexture2D* W:C->GetWeightmapTextures())Textures.AddUnique(W);
            C->ForEachLayer([&](const FGuid&,FLandscapeLayerComponentData& D){for(UTexture2D* W:D.WeightmapData.Textures)Textures.AddUnique(W);});
            for(UTexture2D* W:Textures)
            {
                if(!W||Seen.Contains(W))continue;Seen.Add(W);TArray64<uint8> Bytes;
                if(!W->Source.GetMipData(Bytes,0)||Bytes.Num()>MAX_int32){Error=TEXT("Unable to snapshot weightmap collateral.");return false;}
                Out.Objects.Add(W);Out.Paths.Add(W->GetPathName());Out.Hashes.Add(FCrc::MemCrc32(Bytes.GetData(),int32(Bytes.Num())));
            }
        }
        return true;
    }
    bool SameSnapshot(const FHeightfieldSnapshot& A,const FHeightfieldSnapshot& B)
    {return A.Rect==B.Rect&&A.Extent==B.Extent&&A.Origin==B.Origin&&A.Scale==B.Scale&&A.Heights==B.Heights;}
    bool CollisionMatches(const FTarget& T,const FHeightfieldSnapshot& S,const FPreparedTerrainPlan& P,FString& Error,double WalkThreshold=0,const TArray<uint16>* Planned=nullptr)
    {
        using namespace CollisionCodec;
        if(P.Core.Num()!=2*(S.CountX()-1)*(S.CountY()-1)||P.Transition.Num()!=P.Core.Num())return false;
        const double Start=FPlatformTime::Seconds();double TriangleSeconds=0,TraceSeconds=0,CompareSeconds=0;
        struct FComponentModel {ULandscapeComponent* Component;FModel Model;};
        TArray<FComponentModel> Models;
        // Complete component domain is required for independent min/max. This
        // metadata scan is separate from the frozen 16384 request+halo samples;
        // bounded by Resolve's 262144 total landscape samples and 32MiB mip cache.
        FSourceReader Reader(T,32ll*1024*1024);
        for(ULandscapeComponent* C:T.Actor->LandscapeComponents)
        {
            const FIntPoint Base=C->GetSectionBase();const int32 Q=C->ComponentSizeQuads;
            if(Base.X>S.Rect.Max.X||Base.Y>S.Rect.Max.Y||Base.X+Q<S.Rect.Min.X||Base.Y+Q<S.Rect.Min.Y)continue;
            TArray<uint16> Input;
            // A duplicated shared-edge texel must come from THIS component,
            // not whichever component the legacy global reader finds first.
            if(!Reader.Region(FIntRect(Base,Base+FIntPoint(Q,Q)),Input,Error,C))return false;
            FComponentModel M;M.Component=C;
            // Independently validate all requested source samples against the
            // fresh source scan. Expected data never comes from packed Chaos.
            for(int32 Y=FMath::Max(Base.Y,S.Rect.Min.Y);Y<=FMath::Min(Base.Y+Q,S.Rect.Max.Y);++Y)
                for(int32 X=FMath::Max(Base.X,S.Rect.Min.X);X<=FMath::Min(Base.X+Q,S.Rect.Max.X);++X)
                    if(Input[(Y-Base.Y)*(Q+1)+X-Base.X]!=S.Heights[(Y-S.Rect.Min.Y)*S.CountX()+X-S.Rect.Min.X])
                    {Error=TEXT("Codec source domain does not match verified source snapshot.");return false;}
            if(Planned)
                for(int32 Y=FMath::Max(Base.Y,S.Rect.Min.Y);Y<=FMath::Min(Base.Y+Q,S.Rect.Max.Y);++Y)
                    for(int32 X=FMath::Max(Base.X,S.Rect.Min.X);X<=FMath::Min(Base.X+Q,S.Rect.Max.X);++X)
                        Input[(Y-Base.Y)*(Q+1)+X-Base.X]=(*Planned)[(Y-S.Rect.Min.Y)*S.CountX()+X-S.Rect.Min.X];
            if(!Build(Input,Q+1,Q+1,M.Model))return false;
            Models.Add(MoveTemp(M));
        }
        const double PredictionSeconds=FPlatformTime::Seconds()-Start;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(LandscapeMCPV04Verify),true);Query.bReturnFaceIndex=true;
        // The object under test is the target component's actual physics, not
        // whichever WorldStatic body the ray meets first. Components that are
        // not the expected collision component are stepped past by identity
        // and stay ignored for the rest of this verification; the target hit is
        // still required and compared with unchanged tolerances. The channel
        // query is kept on purpose: a per-body trace would also see the
        // editor-only heightfield shape of the same component.
        constexpr int32 MaxSteppedComponents=256;
        int32 Verified=0,Stepped=0;
        for(int32 Index=0;Index<P.Core.Num();++Index)if(P.Core[Index]||P.Transition[Index])
        {
            double Tick=FPlatformTime::Seconds();
            const int32 Cell=Index/2,GX=S.Rect.Min.X+Cell%(S.CountX()-1),GY=S.Rect.Min.Y+Cell/(S.CountX()-1);
            const bool Upper=Index%2!=0;
            const FComponentModel* M=Models.FindByPredicate([&](const FComponentModel& V){const auto B=V.Component->GetSectionBase();return GX>=B.X&&GY>=B.Y&&GX<B.X+V.Component->ComponentSizeQuads&&GY<B.Y+V.Component->ComponentSizeQuads;});
            if(!M){Error=TEXT("Missing collision component for native triangle.");return false;}
            const auto Base=M->Component->GetSectionBase();const int32 Local=(GY-Base.Y)*M->Model.Cols+GX-Base.X;
            const auto Position=[&](int32 X,int32 Y,int32 I){return FVector(T.Origin.X+X*T.Scale.X,T.Origin.Y+Y*T.Scale.Y,T.Origin.Z+M->Model.Height(I)*T.Scale.Z/128.0);};
            const auto Expected=Triangle(Position(GX,GY,Local),Position(GX+(Upper?0:1),GY+(Upper?1:0),Local+(Upper?M->Model.Cols:1)),Position(GX+1,GY+1,Local+M->Model.Cols+1));
            const double Top=FMath::Max3(Expected.A.Z,Expected.B.Z,Expected.C.Z)+2000;
            const double Bottom=FMath::Min3(Expected.A.Z,Expected.B.Z,Expected.C.Z)-2000;
            TriangleSeconds+=FPlatformTime::Seconds()-Tick;Tick=FPlatformTime::Seconds();
            FHitResult Hit;bool HitOK=false;
            while(!Planned)
            {
                HitOK=T.Actor->GetWorld()->LineTraceSingleByChannel(Hit,FVector(Expected.Centroid.X,Expected.Centroid.Y,Top),FVector(Expected.Centroid.X,Expected.Centroid.Y,Bottom),ECC_WorldStatic,Query);
                UPrimitiveComponent* Other=HitOK?Hit.GetComponent():nullptr;
                if(!Other||Other==M->Component->GetCollisionComponent())break;
                if(Stepped>=MaxSteppedComponents){Error=TEXT("More than 256 other WorldStatic components lie on the collision verification rays.");return false;}
                Query.AddIgnoredComponent(Other);++Stepped;
            }
            TraceSeconds+=FPlatformTime::Seconds()-Tick;Tick=FPlatformTime::Seconds();
            // gamma(128) bound for the double scalar triangle raycast, world/local
            // translations, barycentric arithmetic and normal normalization.
            // Float SIMD bounds only cull cells; GetPointsScaled and the zero-
            // thickness RayTriangleIntersection path use Chaos::FReal (double).
            constexpr double U=2.2204460492503131e-16/2;
            constexpr double Gamma=128*U/(1-128*U);
            const double Magnitude=Expected.A.GetAbs().GetMax()+Expected.B.GetAbs().GetMax()+Expected.C.GetAbs().GetMax()+FMath::Abs(Top)+FMath::Abs(Bottom)+T.Origin.GetAbs().GetMax()+1;
            const double LocationTolerance=Gamma*Magnitude;
            // FHitResult.Location is reconstructed with float HitTime and
            // float CheckLength (CollisionConversions.cpp), unlike ImpactPoint.
            // Three float roundings: distance, segment length, quotient.
            constexpr double UF=5.9604644775390625e-8;
            constexpr double GammaFloat=3*UF/(1-3*UF);
            const double HitLocationTolerance=LocationTolerance+GammaFloat*(Top-Bottom);
            const double NormalTolerance=8*LocationTolerance/FMath::Min(T.Scale.X,T.Scale.Y)+Gamma*16;
            const double ActualSlope=FMath::RadiansToDegrees(FMath::Atan2(FVector2D(Hit.ImpactNormal.X,Hit.ImpactNormal.Y).Size(),Hit.ImpactNormal.Z));
            const double SlopeTolerance=FMath::RadiansToDegrees(2*FMath::Asin(FMath::Min(1.0,NormalTolerance/2)));
            if(WalkThreshold>0&&P.Core[Index])
            {
                const int32 SI=(GY-S.Rect.Min.Y)*S.CountX()+GX-S.Rect.Min.X;
                const auto& Heights=Planned?*Planned:S.Heights;
                auto SA=Expected.A;SA.Z=S.Decode(Heights[SI]);
                auto SB=Expected.B;SB.Z=S.Decode(Heights[SI+(Upper?S.CountX():1)]);
                auto SC=Expected.C;SC.Z=S.Decode(Heights[SI+S.CountX()+1]);
                const auto Guard=Safety(Triangle(SA,SB,SC).Slope,Expected.Slope,WalkThreshold,T.Scale.Z/128.0,M->Model.Step*T.Scale.Z/128.0,T.Scale.X,T.Scale.Y,SlopeTolerance);
                if(!Guard.bSafe){Error=FString::Printf(TEXT("Collision walkability unsafe/near-limit triangle=%d margin=%.12g"),Index,Guard.CombinedSafetyMargin);return false;}
            }
            if(Planned){++Verified;CompareSeconds+=FPlatformTime::Seconds()-Tick;continue;}
            const int32 ExpectedFace=2*((GY-Base.Y)*(M->Model.Cols-1)+GX-Base.X)+(Upper?1:0);
            const bool OK=HitOK&&Hit.GetActor()==T.Actor&&Hit.GetComponent()==M->Component->GetCollisionComponent()
                &&(Hit.FaceIndex==INDEX_NONE||Hit.FaceIndex==ExpectedFace)
                &&FVector::Dist(Hit.ImpactPoint,Expected.Centroid)<=LocationTolerance
                &&FVector::Dist(Hit.Location,Expected.Centroid)<=HitLocationTolerance
                &&FVector::Dist(Hit.ImpactNormal,Expected.Normal)<=NormalTolerance
                &&FMath::Abs(ActualSlope-Expected.Slope)<=SlopeTolerance;
            CompareSeconds+=FPlatformTime::Seconds()-Tick;
            if(!OK)
            {Error=FString::Printf(TEXT("Codec triangle %d mismatch predicted=%.12f actual=%.12f locationDelta=%.12g locationTolerance=%.12g normalDelta=%.12g normalTolerance=%.12g face=%d actor=%s."),Index,Expected.Centroid.Z,Hit.ImpactPoint.Z,FVector::Dist(Hit.ImpactPoint,Expected.Centroid),LocationTolerance,FVector::Dist(Hit.ImpactNormal,Expected.Normal),NormalTolerance,Hit.FaceIndex,*GetNameSafe(Hit.GetActor()));return false;}
            ++Verified;
        }
        UE_LOG(LogTemp,Display,TEXT("COLLISION_CODEC_PERF components=%d sourceMipBytes=%lld triangles=%d predictionMs=%.6f triangleMs=%.6f traceMs=%.6f compareMs=%.6f totalMs=%.6f steppedComponents=%d"),Models.Num(),Reader.GetCachedBytes(),Verified,PredictionSeconds*1000,TriangleSeconds*1000,TraceSeconds*1000,CompareSeconds*1000,(FPlatformTime::Seconds()-Start)*1000,Stepped);
        return true;
    }
    void DerivedUpdate(const FTarget& T)
    {T.Actor->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Heightmap_All);T.Actor->ForceUpdateLayersContent();}
    FLandscapeMCPTerrainOperationResult MakeFailure(FLandscapeMCPTerrainOperationResult O,const FString& Code,const TCHAR* Stage,const FString& Reason)
    {
        O.bSuccess=false;O.bActualValid=false;O.bWalkabilityGuaranteed=false;O.ModifiedCount=0;O.ErrorCode=Code;
        O.ActualAfter={};O.TransitionActualAfter={};
        O.Status=TEXT("REJECTED");O.Message=FString(Stage)+TEXT(": ")+Reason;return O;
    }
}
bool MutationDisabled(){return bMutationDisabled;}
#if WITH_DEV_AUTOMATION_TESTS
bool VerifyAutomationCollision(const FString& Path,const FHeightfieldSnapshot& S,const FPreparedTerrainPlan& P,FString& Error)
{check(GIsAutomationTesting);FTarget T;if(!Resolve(Path,T,Error))return false;return CollisionMatches(T,S,P,Error);}
void SetAutomationApplyFault(EApplyFault F){check(GIsAutomationTesting);AutomationFault=F;}
void ResetAutomationApplyState(){check(GIsAutomationTesting);AutomationFault=EApplyFault::None;bMutationDisabled=false;RetryRecords.Reset();}
int32 AutomationRetryRecordCount(){check(GIsAutomationTesting);return RetryRecords.Num();}
#endif
FString FormatFailureDiagnostic(const FLandscapeMCPTerrainOperationResult& Result,const FString& InStage)
{
    // Profile arrays are never included. Apply messages are "<stage>: <reason>";
    // only that exact, known prefix is removed, so a colon inside the reason
    // (or a Dry-run message without a stage prefix) cannot change the stage.
    const FString Stage=InStage.IsEmpty()?FString(TEXT("validation")):InStage;
    FString Reason=Result.Message;Reason.RemoveFromStart(Stage+TEXT(": "),ESearchCase::CaseSensitive);
    TSharedRef<FJsonObject> Diagnostic=MakeShared<FJsonObject>();
    Diagnostic->SetStringField(TEXT("errorCode"),Result.ErrorCode);
    Diagnostic->SetStringField(TEXT("operation"),Result.Operation);
    Diagnostic->SetStringField(TEXT("stage"),Stage.Left(64));
    Diagnostic->SetStringField(TEXT("landscapePath"),Result.LandscapePath.Left(1024));
    Diagnostic->SetStringField(TEXT("reason"),Reason.Left(2048));
    Diagnostic->SetBoolField(TEXT("mutationDisabled"),MutationDisabled());
    Diagnostic->SetStringField(TEXT("message"),Result.Message.Left(2048));
    Diagnostic->SetStringField(TEXT("rollbackStatus"),Result.RollbackStatus);
    FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Diagnostic,Writer);
    return Text;
}
FLandscapeMCPTerrainOperationResult Execute(const FPreviewRequest& R,FString* OutFailureStage)
{
    if(OutFailureStage)OutFailureStage->Reset();
    if(R.bDryRun)return Preview(R,OutFailureStage);
    auto Failed=[&](FLandscapeMCPTerrainOperationResult Result,const FString& FailureCode,const TCHAR* Stage,const FString& Reason)
    {if(OutFailureStage)*OutFailureStage=Stage;return MakeFailure(MoveTemp(Result),FailureCode,Stage,Reason);};
    const double ApplyStart=FPlatformTime::Seconds();
    FLandscapeMCPTerrainOperationResult O;O.bDryRun=false;O.LandscapePath=R.LandscapePath;
    O.Operation=R.Operation==EAuthoringOperation::FlattenRectangle?TEXT("FlattenRectangle"):R.Operation==EAuthoringOperation::FlattenPath?TEXT("FlattenPath"):TEXT("CreateWalkablePath");
    if(!IsInGameThread())return Failed(O,TEXT("EDITOR_STATE"),TEXT("validation"),TEXT("Game thread required."));
    if(bMutationDisabled)return Failed(O,TEXT("MUTATION_DISABLED_AFTER_ROLLBACK_FAILURE"),TEXT("safetyLatch"),TEXT("Restart Editor or reload module after investigating failed restoration."));
    FPreviewRequest PreviewRequest=R;PreviewRequest.bDryRun=true;
    FPath Path;FVector2D Min,Max;FString Code,Error;
    if(!ValidateRequest(PreviewRequest,Path,Min,Max,Code))return Failed(O,Code,TEXT("validation"),Code);
    FTarget Target;FIdentity Identity;FCollateral Collateral;
    if(!CaptureTarget(R.LandscapePath,Target,Identity,Code,Error))return Failed(O,Code,TEXT("resolve"),Error);
    FHeightfieldSnapshot Original;
    if(!ReadSnapshot(R.LandscapePath,Min,Max,Original,Code,Error))return Failed(O,Code,TEXT("snapshot"),Error);
    if(!CaptureCollateral(Target,Collateral,Error))return Failed(O,TEXT("UNSUPPORTED_COLLATERAL_STATE"),TEXT("snapshot"),Error);
    const int32 Cached=RetryRecords.IndexOfByPredicate([&](const FRetryRecord& Record){return SameRequest(Record.Request,R);});
    if(Cached!=INDEX_NONE)
    {
        const FRetryRecord& Record=RetryRecords[Cached];
        if(Record.Identity==Identity&&SameSnapshot(Record.ResultSnapshot,Original))
        {
            // Duplicate of a verified request on unchanged terrain. Target guards,
            // source and identity above are fresh; actual collision is re-verified.
            if(!CollisionMatches(Target,Original,Record.Plan,Error,R.Operation==EAuthoringOperation::CreateWalkablePath?R.MaxSlope:0))return Failed(O,TEXT("COLLISION_SYNC_FAILED"),TEXT("retry"),Error);
            O=Record.Result;O.Status=TEXT("NO_CHANGE");O.ModifiedCount=0;O.PlannedModifiedCount=0;
            O.Before=O.PlannedAfter=O.ActualAfter;O.TransitionBefore=O.TransitionPlannedAfter=O.TransitionActualAfter;
            O.Delta.MinDeltaCm=O.Delta.MaxDeltaCm=O.Delta.MaxRaiseCm=O.Delta.MaxLowerCm=0;
            for(auto& Sample:O.Path.ProfileSamples)Sample.CurrentHeightCm=Sample.PlannedHeightCm;
            O.Message=TEXT("retry: Identical request already verified at this source state; no transaction or write.");return O;
        }
        // The terrain or target no longer equals the recorded result (Undo, a
        // later edit, ...). The record proves nothing about the current state:
        // drop it and treat the request as new.
        RetryRecords.RemoveAt(Cached);
    }
    RetryRecords.Reserve(MaxRetryRecords);
    const double PlanningStart=FPlatformTime::Seconds();
    FPreparedTerrainPlan Plan;O=PlanPreview(PreviewRequest,Original,&Plan);O.bDryRun=false;
    if(!O.bSuccess){if(OutFailureStage)*OutFailureStage=TEXT("planning");O.Message=TEXT("planning: ")+O.Message;return O;}
    const double PlanningMs=(FPlatformTime::Seconds()-PlanningStart)*1000;
    O.Warnings.RemoveAll([](const FString& W){return W.StartsWith(TEXT("PREVIEW_ONLY:"));});
    if(!CollisionMatches(Target,Original,Plan,Error))return Failed(O,TEXT("UNSUPPORTED_COLLISION_STATE"),TEXT("beforeCollision"),Error);
    if(!CollisionMatches(Target,Original,Plan,Error,R.Operation==EAuthoringOperation::CreateWalkablePath?R.MaxSlope:0,&Plan.Quantized))
        return Failed(O,TEXT("CANNOT_SATISFY_WALKABILITY"),TEXT("predictedCollision"),Error);
    // Allocate the complete retry record before entering the write transaction.
    FRetryRecord Pending;Pending.Request=R;Pending.Identity=Identity;Pending.Plan=Plan;Pending.ResultSnapshot=Original;
    Pending.ResultSnapshot.Heights=Plan.Quantized;
    const double PreStart=FPlatformTime::Seconds();
    FTarget Immediate;FIdentity ImmediateIdentity;FCollateral ImmediateCollateral;FHeightfieldSnapshot ImmediateSource;
    const bool ImmediateRead=CaptureTarget(R.LandscapePath,Immediate,ImmediateIdentity,Code,Error)
        &&ReadSnapshot(R.LandscapePath,Min,Max,ImmediateSource,Code,Error);
    if(Fault(EApplyFault::StaleSource)&&!ImmediateSource.Heights.IsEmpty())ImmediateSource.Heights[0]^=1;
    if(bMutationDisabled||!ImmediateRead||!(ImmediateIdentity==Identity)||!SameSnapshot(Original,ImmediateSource)
        ||!CaptureCollateral(Immediate,ImmediateCollateral,Error)||!(Collateral==ImmediateCollateral))
        return Failed(O,TEXT("STALE_PLAN"),TEXT("preWrite"),TEXT("Target, source, safety state, transaction or collateral changed since planning."));
    const double PreMs=(FPlatformTime::Seconds()-PreStart)*1000;
    if(O.PlannedModifiedCount==0)
    {
        if(!VerifyPreparedGeometry(R,Original,ImmediateSource,Plan,O))return Failed(O,TEXT("POST_VERIFY_FAILED"),TEXT("noChange"),TEXT("Fresh geometry verification failed."));
        if(!CollisionMatches(Target,ImmediateSource,Plan,Error,R.Operation==EAuthoringOperation::CreateWalkablePath?R.MaxSlope:0))return Failed(O,TEXT("COLLISION_SYNC_FAILED"),TEXT("noChangeCollision"),Error);
        O.Status=TEXT("NO_CHANGE");O.Message=TEXT("noChange: Target already satisfies the verified plan; no transaction or write.");
        Pending.Result=O;RememberRetry(MoveTemp(Pending));return O;
    }
    FGuid Owned;FString OwnedContext=TEXT("LandscapeMCP.V04.")+FGuid::NewGuid().ToString();
    const FText Title=FText::FromString(TEXT("Landscape MCP v0.4 ")+O.Operation);
    double WriteMs=0,FlushMs=0;
    {
        FScopedTransaction Transaction(*OwnedContext,Title,Target.Actor);
        if(!Transaction.IsOutstanding()||!GUndo)return Failed(O,TEXT("TRANSACTION_FAILED"),TEXT("transaction"),TEXT("Could not start an owned transaction."));
        Owned=GUndo->GetContext().TransactionId;
        if(!Owned.IsValid()){Transaction.Cancel();return Failed(O,TEXT("TRANSACTION_FAILED"),TEXT("transaction"),TEXT("Owned transaction has no valid GUID."));}
        Target.Actor->Modify();Target.Info->Modify();
        const double WriteStart=FPlatformTime::Seconds();
        {
            FScopedSetLandscapeEditingLayer LayerScope(Target.Actor,Target.LayerGuid);
            FLandscapeEditDataInterface EditData(Target.Info,Target.LayerGuid);
            EditData.SetHeightData(Original.Rect.Min.X,Original.Rect.Min.Y,Original.Rect.Max.X,Original.Rect.Max.Y,Plan.Quantized.GetData(),0,false,
                nullptr,nullptr,nullptr,false,nullptr,nullptr,true,true,true);
            EditData.Flush();
        }
        WriteMs=(FPlatformTime::Seconds()-WriteStart)*1000;
        const double FlushStart=FPlatformTime::Seconds();DerivedUpdate(Target);FlushMs=(FPlatformTime::Seconds()-FlushStart)*1000;
    }
    const double SourceStart=FPlatformTime::Seconds();FHeightfieldSnapshot Actual;FTarget PostTarget;FIdentity PostIdentity;FCollateral PostCollateral;
    const bool FreshRead=!Fault(EApplyFault::FreshReadFailure)&&ReadSnapshot(R.LandscapePath,Min,Max,Actual,Code,Error);
    if(Fault(EApplyFault::SourceMismatch)&&!Actual.Heights.IsEmpty())Actual.Heights[0]^=1;
    const bool SourceOK=FreshRead&&Actual.Heights==Plan.Quantized
        &&CaptureTarget(R.LandscapePath,PostTarget,PostIdentity,Code,Error)&&PostIdentity==Identity
        &&CaptureCollateral(PostTarget,PostCollateral,Error)&&PostCollateral==Collateral;
    const double SourceMs=(FPlatformTime::Seconds()-SourceStart)*1000;
    const double GeometryStart=FPlatformTime::Seconds();
    bool GeometryOK=SourceOK&&VerifyPreparedGeometry(R,Original,Actual,Plan,O)&&!Fault(EApplyFault::GeometryMismatch);
    const double GeometryMs=(FPlatformTime::Seconds()-GeometryStart)*1000;
    const double CollisionStart=FPlatformTime::Seconds();
    const bool CollisionOK=GeometryOK&&CollisionMatches(PostTarget,Actual,Plan,Error,R.Operation==EAuthoringOperation::CreateWalkablePath?R.MaxSlope:0)&&!Fault(EApplyFault::CollisionFailure);
    const double CollisionMs=(FPlatformTime::Seconds()-CollisionStart)*1000;
    const bool ForceRollback=Fault(EApplyFault::UndoFalse)||Fault(EApplyFault::RestoreMismatch)||Fault(EApplyFault::RestoreCollisionFailure)||Fault(EApplyFault::RestoreCodecMismatch)||Fault(EApplyFault::RestoreCollisionStale)
        ||Fault(EApplyFault::OwnershipMismatch)||Fault(EApplyFault::InsertUnrelatedTransaction);
    const FTransactionContext BeforeCommit=GEditor->Trans->GetUndoContext();
    const bool StillOwn=BeforeCommit.TransactionId==Owned&&BeforeCommit.Context==OwnedContext&&BeforeCommit.PrimaryObject==Target.Actor;
    if(SourceOK&&GeometryOK&&CollisionOK&&StillOwn&&!ForceRollback)
    {
        O.Status=TEXT("APPLIED");O.bSuccess=true;O.ErrorCode.Reset();O.Message=TEXT("applied: Fresh source, geometry and native collision verified; one owned transaction; nothing saved.");
        Pending.Result=O;RememberRetry(MoveTemp(Pending));
        UE_LOG(LogTemp,Display,TEXT("LandscapeMCP timing planningMs=%.3f preWriteMs=%.3f writeMs=%.3f flushMs=%.3f postSourceMs=%.3f geometryMs=%.3f collisionMs=%.3f totalApplyMs=%.3f triangles=%d"),PlanningMs,PreMs,WriteMs,FlushMs,SourceMs,GeometryMs,CollisionMs,(FPlatformTime::Seconds()-ApplyStart)*1000,Plan.Core.Num());
        return O;
    }
    const FString Failure=SourceOK?(GeometryOK?TEXT("COLLISION_SYNC_FAILED"):TEXT("POST_GEOMETRY_VERIFY_FAILED")):TEXT("POST_VERIFY_FAILED");
    O=Failed(O,Failure,SourceOK?(GeometryOK?TEXT("postCollision"):TEXT("postGeometry")):TEXT("postSource"),Error.IsEmpty()?TEXT("Post verification failed."):Error);
#if WITH_DEV_AUTOMATION_TESTS
    if(Fault(EApplyFault::InsertUnrelatedTransaction))
    {FScopedTransaction Other(TEXT("LandscapeMCP.Automation.Unrelated"),INVTEXT("Unrelated Automation transaction"),Target.Actor);Target.Actor->Modify();Target.Actor->SetActorLabel(TEXT("Automation unrelated label edit"));}
#endif
    const double RollbackStart=FPlatformTime::Seconds();
    const FTransactionContext Latest=GEditor->Trans->GetUndoContext();
    const bool Own=Owned.IsValid()&&!GEditor->IsTransactionActive()&&Latest.TransactionId==Owned&&Latest.Context==OwnedContext
        &&Latest.Title.EqualTo(Title)&&Latest.PrimaryObject==Target.Actor&&!Fault(EApplyFault::OwnershipMismatch);
    bool Restored=false;FString RestoreError=Own?FString():FString(TEXT("Ownership of the latest transaction could not be proved; no Undo was attempted."));
    if(Own&&!Fault(EApplyFault::UndoFalse)&&GEditor->UndoTransaction(false))
    {
        DerivedUpdate(Target);
        FTarget RestoredTarget;FIdentity RestoredIdentity;FCollateral RestoredCollateral;FHeightfieldSnapshot RestoredSource;
        const bool RestoreRead=CaptureTarget(R.LandscapePath,RestoredTarget,RestoredIdentity,Code,RestoreError)
            &&ReadSnapshot(R.LandscapePath,Min,Max,RestoredSource,Code,RestoreError);
        if(Fault(EApplyFault::RestoreMismatch)&&!RestoredSource.Heights.IsEmpty())RestoredSource.Heights[0]^=1;
#if WITH_DEV_AUTOMATION_TESTS
        // Targeted fixture-only corruption of ACTUAL physics, never the expected
        // model or source. Demonstrates true Undo is insufficient proof.
        if(RestoreRead&&(Fault(EApplyFault::RestoreCodecMismatch)||Fault(EApplyFault::RestoreCollisionStale)))
        {
            auto* C=RestoredTarget.Actor->LandscapeComponents[0]->GetCollisionComponent();
            if(C&&C->HeightfieldRef.IsValid()&&C->HeightfieldRef->HeightfieldGeometry.IsValid())
            {
                auto& H=*C->HeightfieldRef->HeightfieldGeometry;
                if(Fault(EApplyFault::RestoreCollisionStale))H.GeomData.HeightPerUnit*=2;
                else
                {
                    const int32 I=30*H.GetNumCols()+30;
                    if(H.GeomData.Heights.IsValidIndex(I))H.GeomData.Heights[I]^=8192;
                }
                H.GeomData.BuildLowResolutionData();H.BoundingBox();
            }
        }
#endif
        Restored=RestoreRead&&RestoredIdentity==Identity&&SameSnapshot(Original,RestoredSource)
            &&CaptureCollateral(RestoredTarget,RestoredCollateral,RestoreError)&&RestoredCollateral==Collateral
            &&CollisionMatches(RestoredTarget,RestoredSource,Plan,RestoreError)&&!Fault(EApplyFault::RestoreCollisionFailure);
    }
    O.Status=Restored?TEXT("ROLLED_BACK"):TEXT("ROLLBACK_FAILED");O.RollbackStatus=Restored?TEXT("VERIFIED"):TEXT("FAILED");
    if(!Restored){bMutationDisabled=true;O.Message+=TEXT("; restoreVerification: ")+(RestoreError.IsEmpty()?FString(TEXT("Restored source, identity, collateral or collision did not match the original, or Undo failed.")):RestoreError);O.Message+=TEXT("; rollback: Restoration could not be proved; v0.4 mutation disabled for this module session.");}
    UE_LOG(LogTemp,Display,TEXT("LandscapeMCP rollback own=%d restored=%d elapsedMs=%.3f preWriteMs=%.3f writeMs=%.3f flushMs=%.3f postSourceMs=%.3f collisionMs=%.3f"),Own,Restored,(FPlatformTime::Seconds()-RollbackStart)*1000,PreMs,WriteMs,FlushMs,SourceMs,CollisionMs);
    return O;
}
}
