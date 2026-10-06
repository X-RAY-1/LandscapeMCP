#include "LandscapeMCPOperations.h"
#include "LandscapeMCPTerrainAnalysis.h"
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
        { Error = TEXT("World Partition / external-actor Levels are unsupported in v0.1."); return false; }
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
        if (T.Actor->IsNaniteEnabled()) { Error = TEXT("Nanite Landscapes are unsupported in v0.1."); return false; }
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
        { Error = TEXT("Landscape exceeds the v0.1 total sample limit."); return false; }
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
        explicit FSourceReader(const FTarget& InTarget) : T(InTarget) {}
        bool Read(int32 X, int32 Y, uint16& Value, FString& Error)
        {
            ULandscapeComponent* Component = nullptr;
            for (ULandscapeComponent* C : T.Actor->LandscapeComponents)
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
                TArray64<uint8> Copy;
                if (!Texture->Source.GetMipData(Copy, 0) || Copy.Num() != int64(Texture->Source.GetSizeX()) * Texture->Source.GetSizeY() * sizeof(FColor))
                { Error = TEXT("Unable to read complete source heightmap mip."); return false; }
                Bytes = &Cache.Add(Texture, MoveTemp(Copy));
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
        bool Region(const FIntRect& Rect, TArray<uint16>& Values, FString& Error)
        {
            Values.SetNumUninitialized((Rect.Width()+1)*(Rect.Height()+1));
            int32 Index = 0;
            for (int32 Y = Rect.Min.Y; Y <= Rect.Max.Y; ++Y)
                for (int32 X = Rect.Min.X; X <= Rect.Max.X; ++X)
                    if (!Read(X,Y,Values[Index++],Error)) { return false; }
            return true;
        }
    private:
        const FTarget& T;
        TMap<UTexture2D*, TArray64<uint8>> Cache;
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
    bool MeasureSlope(const FString& Path, double X, double Y, double D, FSlopeMeasurement& Out, FString& Error)
    {
        if (!Coordinate(X) || !Coordinate(Y)) { Error = TEXT("Non-finite / out-of-range world XY."); return false; }
        if (!FMath::IsFinite(D) || D < MinSampleDistance || D > MaxSampleDistance)
        { Error = TEXT("SampleDistanceCm must be finite and within [1,5000]."); return false; }
        FTarget T;
        if (!Resolve(Path,T,Error)) { return false; }
        FSourceReader Reader(T); bool bOutside = false;
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
    R.HeightsCm = MoveTemp(Heights);
    R.Message = TEXT("Read-only bilinear source heights on a regular grid (row-major, Y outer). No package or transaction changes.");
    return R;
}
}
