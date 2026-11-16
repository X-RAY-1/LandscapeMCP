#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "LandscapeMCPApply.h"
#include "LandscapeMCPOperations.h"
#include "LandscapeMCPFoliageImpact.h"
#include "LandscapeMCPEngineAdapter.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeEditLayer.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "LandscapeSettings.h"
#include "InstancedFoliageActor.h"
#include "InstancedFoliage.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "ProceduralFoliageComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Serialization/ObjectWriter.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationEditorCommon.h"
#include "ToolsetRegistry/ToolsetLibrary.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "LandscapeMCPToolset.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/UnrealType.h"
#include "LandscapeEdit.h"
#include "LandscapeInfo.h"
#include "FoliageType_Actor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/LevelStreamingDynamic.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"
#include "ToolsetRegistry/ToolCallAsyncResultString.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "LandscapeStreamingProxy.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionLandscapeGrassOutput.h"
#include "Materials/MaterialExpressionMaterialAttributeLayers.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionInstance.h"

// v0.5 Foliage Safety。Foliageを壊し得る操作は、ここで作る未保存Mapの中だけで行う。
namespace LandscapeMCP::FoliageTests
{
    using namespace Authoring;
    uint32 Hash(UObject* O){TArray<uint8> Bytes;FObjectWriter W(O,Bytes);return FCrc::MemCrc32(Bytes.GetData(),Bytes.Num());}
    struct FFixture
    {
        UWorld* World=nullptr;ALandscape* Actor=nullptr;AInstancedFoliageActor* IFA=nullptr;UFoliageType* Type=nullptr;FFoliageInfo* Info=nullptr;
        FString Path;int32 Saves=0;FDelegateHandle SaveHandle;
        explicit FFixture(int32 Components=1)
        {
            ResetAutomationApplyState();World=FAutomationEditorCommonUtils::CreateNewMap();if(!World)return;
            const auto C=Create(World->PersistentLevel->GetPathName(),TEXT("V05FoliageFixture"),FVector::ZeroVector,FVector(100),Components,Components,1,63,0,false);
            if(!C.bSuccess)return;Path=C.LandscapePath;Actor=FindObject<ALandscape>(nullptr,*Path);
            SaveHandle=FCoreUObjectDelegates::OnObjectPreSave.AddLambda([this](UObject*,FObjectPreSaveContext){++Saves;});
            IFA=AInstancedFoliageActor::GetInstancedFoliageActorForLevel(World->PersistentLevel,true);
            UStaticMesh* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
            if(IFA&&Mesh)Info=IFA->AddMesh(Mesh,&Type);
        }
        ~FFixture()
        {
            FCoreUObjectDelegates::OnObjectPreSave.Remove(SaveHandle);SetAutomationApplyFault(EApplyFault::None);
            while(GEditor&&GEditor->Trans&&GEditor->Trans->CanUndo())
            {
                const auto C=GEditor->Trans->GetUndoContext();
                if(!C.Context.StartsWith(TEXT("LandscapeMCP.V04."))&&!C.Context.StartsWith(TEXT("LandscapeMCP.Automation.")))break;
                if(!GEditor->UndoTransaction(false))break;
            }
            // 2つ目のfixtureが新Mapを作ると前のWorldは破棄される。今のEditor Worldに居る場合だけ流す。
            if(GEditor&&World&&GEditor->GetEditorWorldContext().World()==World&&IsValid(Actor))Actor->ForceUpdateLayersContent();
            ResetAutomationApplyState();
        }
        bool Valid()const{return Actor&&IFA&&Type&&Info;}
        // 対象Landscapeのcollision componentへ付属させたinstance（UEのsnapの対象になる形）。
        void Add(FVector Location,bool bAttach=true)
        {
            UActorComponent* Base=nullptr;
            if(bAttach)
                for(ULandscapeComponent* C:Actor->LandscapeComponents)
                {
                    const FIntPoint B=C->GetSectionBase();const double X=Location.X/100,Y=Location.Y/100;
                    if(X>=B.X&&Y>=B.Y&&X<=B.X+C->ComponentSizeQuads&&Y<=B.Y+C->ComponentSizeQuads){Base=C->GetCollisionComponent();break;}
                }
            FFoliageInstance I;I.Location=Location;I.BaseComponent=Base;Info->AddInstance(Type,I,Base);
        }
        FPreviewRequest Rect(double Target=100,bool bDry=false)
        {
            FPreviewRequest R;R.Operation=EAuthoringOperation::FlattenRectangle;R.LandscapePath=Path;R.bDryRun=bDry;
            R.Center=FVector2D(3000,3000);R.SizeX=1000;R.SizeY=800;R.Falloff=300;R.MaxDelta=300;R.TargetHeight=Target;return R;
        }
        FHeightfieldSnapshot Read(FVector2D Min=FVector2D(0,0),FVector2D Max=FVector2D(6300,6300))
        {FHeightfieldSnapshot S;FString Code,Error;ReadSnapshot(Path,Min,Max,S,Code,Error);return S;}
        TArray<FFoliageInstance> Instances()const{return Info?Info->Instances:TArray<FFoliageInstance>();}
    };
    bool SameTransforms(const TArray<FFoliageInstance>& A,const TArray<FFoliageInstance>& B)
    {
        if(A.Num()!=B.Num())return false;
        for(int32 I=0;I<A.Num();++I)
            if(A[I].Location!=B[I].Location||A[I].Rotation!=B[I].Rotation||!(A[I].DrawScale3D==B[I].DrawScale3D)||A[I].ZOffset!=B[I].ZOffset||A[I].BaseId!=B[I].BaseId)return false;
        return true;
    }
    // Project設定はテストの中だけで変え、必ず元に戻して戻ったことを確認する（副作用の監査）。
    struct FScopedSnapMode
    {
        FAutomationTestBase& Test;FEnumProperty* Property=nullptr;void* Value=nullptr;int64 Original=0;
        FScopedSnapMode(FAutomationTestBase& T,EFoliageLandscapeSnappingMode Mode):Test(T)
        {
            ULandscapeSettings* Settings=GetMutableDefault<ULandscapeSettings>();
            Property=FindFProperty<FEnumProperty>(ULandscapeSettings::StaticClass(),TEXT("FoliageLandscapeSnappingMode"));
            if(!Settings||!Property)return;
            Value=Property->ContainerPtrToValuePtr<void>(Settings);Original=Property->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value);
            Property->GetUnderlyingProperty()->SetIntPropertyValue(Value,int64(Mode));
        }
        bool Valid()const{return Property&&Value;}
        ~FScopedSnapMode()
        {
            if(!Valid())return;Property->GetUnderlyingProperty()->SetIntPropertyValue(Value,Original);
            Test.TestEqual(TEXT("snapping mode restored"),Property->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value),Original);
        }
    };
    Authoring::FHeightfieldSnapshot Flat(int32 N,uint16 H=32768)
    {
        Authoring::FHeightfieldSnapshot S;S.Origin=FVector::ZeroVector;S.Scale=FVector(100);
        S.Extent=S.Rect=FIntRect(0,0,N-1,N-1);S.Heights.Init(H,N*N);return S;
    }
    Foliage::FSample Sample(FVector L,Foliage::EBase Base=Foliage::EBase::TargetLandscape)
    {Foliage::FSample S;S.FoliageType=TEXT("/Test/Type");S.Location=L;S.Base=Base;return S;}
}
using namespace LandscapeMCP;
using namespace LandscapeMCP::Authoring;
using namespace LandscapeMCP::FoliageTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05Evaluator,"LandscapeMCP.V05.Foliage.Evaluator",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05Evaluator::RunTest(const FString&)
{
    // UObjectを使わない評価の単体テスト。11x11、1m間隔、平坦（高さ0）。
    const auto S=Flat(11);const double Q=S.Scale.Z/128;
    TArray<uint16> Up=S.Heights;Up[5*11+5]+=128;          // (500,500)を+100cm
    TArray<uint16> Down=S.Heights;Down[5*11+5]-=128;      // (500,500)を-100cm
    TArray<uint16> Tiny=S.Heights;Tiny[5*11+5]+=0;        // 変化なし
    Foliage::FInventory Inv;Inv.SnappingMode=TEXT("DontSnap");Inv.bSnappingSafe=true;
    Inv.Samples={Sample(FVector(500,500,0)),Sample(FVector(450,520,0)),Sample(FVector(900,900,0)),Sample(FVector(100,100,0),Foliage::EBase::Unattached)};
    auto O=Foliage::Evaluate(S,Up,Inv);
    TestEqual(TEXT("vertex buried"),O.BuriedCount,2);TestEqual(TEXT("far unchanged"),O.UnchangedCount,2);
    TestEqual(TEXT("buried amount"),O.MaxBuryCm,100.0);TestEqual(TEXT("blocked"),O.Verdict,FString(TEXT("BLOCKED")));
    TestEqual(TEXT("only non-unchanged reported"),O.Samples.Num(),2);
    O=Foliage::Evaluate(S,Down,Inv);TestEqual(TEXT("floating"),O.FloatingCount,2);TestEqual(TEXT("float amount"),O.MaxFloatCm,100.0);
    // 量子化の半段以下の差は変化とみなさない（Planned=現在）。
    O=Foliage::Evaluate(S,Tiny,Inv);TestEqual(TEXT("no change safe"),O.Verdict,FString(TEXT("SAFE_TO_APPLY")));TestEqual(TEXT("all unchanged"),O.UnchangedCount,4);
    // 高さは同じで傾斜だけが変わる: 隣接頂点を上げ、instanceの頂点(300,300)は動かさない。
    TArray<uint16> Tilt=S.Heights;Tilt[3*11+4]+=64;
    Foliage::FInventory One=Inv;One.Samples={Sample(FVector(300,300,0))};
    O=Foliage::Evaluate(S,Tilt,One);TestEqual(TEXT("slope changed at an unchanged vertex"),O.SlopeChangedCount,1);TestEqual(TEXT("slope blocked"),O.Verdict,FString(TEXT("BLOCKED")));
    // 判定不能なinstanceと範囲の判定不能。
    Foliage::FInventory Unknown=Inv;Unknown.Samples[0].UnknownReason=TEXT("PROCEDURAL_INSTANCE");
    O=Foliage::Evaluate(S,Tiny,Unknown);TestEqual(TEXT("unknown instance"),O.UnknownCount,1);TestEqual(TEXT("unknown verdict"),O.Verdict,FString(TEXT("UNKNOWN")));
    Foliage::FInventory Grass=Inv;Grass.Unknowns={TEXT("LANDSCAPE_GRASS: x")};
    TestEqual(TEXT("region unknown"),Foliage::Evaluate(S,Tiny,Grass).Verdict,FString(TEXT("UNKNOWN")));
    Foliage::FInventory Limit=Inv;Limit.bScanLimitExceeded=true;Limit.Samples.Reset();
    TestEqual(TEXT("scan limit is unknown"),Foliage::Evaluate(S,Tiny,Limit).Verdict,FString(TEXT("UNKNOWN")));
    // snap modeが有効なら、地形が変わらなくても付属instanceは動き得る。範囲内に無くても付属があれば拒否。
    Foliage::FInventory Snap=Inv;Snap.SnappingMode=TEXT("SnapEverywhere");Snap.bSnappingSafe=false;Snap.bTargetHasAttachedFoliage=true;
    O=Foliage::Evaluate(S,Tiny,Snap);TestEqual(TEXT("attached may move"),O.MayBeMovedCount,3);TestEqual(TEXT("unattached unchanged"),O.UnchangedCount,1);
    TestEqual(TEXT("snap blocked"),O.Verdict,FString(TEXT("BLOCKED")));
    Snap.Samples.Reset();TestEqual(TEXT("snap blocked without samples"),Foliage::Evaluate(S,Tiny,Snap).Verdict,FString(TEXT("BLOCKED")));
    Foliage::FInventory None;None.SnappingMode=TEXT("DontSnap");None.bSnappingSafe=true;
    TestEqual(TEXT("no foliage"),Foliage::Evaluate(S,Up,None).Verdict,FString(TEXT("NO_FOLIAGE")));
    // 境界: 範囲外のinstanceは地形を読めないので判定不能（安全とはみなさない）。
    Foliage::FInventory Outside=Inv;Outside.Samples={Sample(FVector(5000,5000,0),Foliage::EBase::Unattached)};
    TestEqual(TEXT("outside snapshot unknown"),Foliage::Evaluate(S,Up,Outside).Verdict,FString(TEXT("UNKNOWN")));
    // 1セル離れたinstanceは変化なし（bilinearは隣のセルへ広がらない）。
    Foliage::FInventory Edge=Inv;Edge.Samples={Sample(FVector(600,500,0)),Sample(FVector(400,400,0)),Sample(FVector(650,500,0))};
    O=Foliage::Evaluate(S,Up,Edge);TestEqual(TEXT("cell edge samples"),O.BuriedCount,0);TestEqual(TEXT("cell edge unchanged or slope"),O.UnchangedCount+O.SlopeChangedCount,3);
    TestTrue(TEXT("same instances"),Foliage::SameInstances(Inv,Inv));
    Foliage::FInventory Moved=Inv;Moved.Samples[0].Location.Z+=1.e-6;TestFalse(TEXT("a moved instance differs"),Foliage::SameInstances(Inv,Moved));
    AddInfo(FString::Printf(TEXT("quantization=%.6f"),Q));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05NoFoliage,"LandscapeMCP.V05.Foliage.NoFoliageParity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05NoFoliage::RunTest(const FString&)
{
    FFixture F;if(!TestTrue(TEXT("fixture"),F.Actor!=nullptr))return false;
    // AddMeshだけでinstanceがないIFAは「Foliageなし」と同じ。
    const auto Dry=Execute(F.Rect(100,true));TestTrue(*Dry.Message,Dry.bSuccess);
    TestEqual(TEXT("no foliage verdict"),Dry.Foliage.Verdict,FString(TEXT("NO_FOLIAGE")));TestTrue(TEXT("evaluated"),Dry.Foliage.bEvaluated);
    TestFalse(TEXT("no foliage warning"),Dry.Warnings.ContainsByPredicate([](const FString& W){return W.StartsWith(TEXT("FOLIAGE_"));}));
    const int32 Undo=UToolsetLibrary::GetActiveUndoCount();
    const auto O=Execute(F.Rect());TestEqual(*O.Message,O.Status,FString(TEXT("APPLIED")));
    TestEqual(TEXT("dry run plan equals apply"),O.ModifiedCount,Dry.PlannedModifiedCount);
    TestTrue(TEXT("post apply checked"),O.Foliage.PostApply.bChecked);TestTrue(TEXT("instances unchanged"),O.Foliage.PostApply.bInstancesUnchanged);
    TestFalse(TEXT("v0.4 path: no explicit deferred flush"),O.Foliage.PostApply.bDeferredUpdateChecked);
    TestEqual(TEXT("one transaction"),UToolsetLibrary::GetActiveUndoCount(),Undo+1);TestEqual(TEXT("no saves"),F.Saves,0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05Outside,"LandscapeMCP.V05.Foliage.OutsideAffectedUndoRedoDeferred",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05Outside::RunTest(const FString&)
{
    FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
    // 影響範囲（約2200..3800 x 2300..3700）と読み取りhaloの外に、付属instanceを置く。
    F.Add(FVector(500,500,0));F.Add(FVector(6000,6000,0));F.Add(FVector(500,6000,0),false);
    if(!TestTrue(TEXT("attached fixture"),AInstancedFoliageActor::HasFoliageAttached(F.Actor->LandscapeComponents[0]->GetCollisionComponent())))return false;
    const auto Instances=F.Instances();const uint32 IFAHash=Hash(F.IFA);const auto Original=F.Read();
    const auto Dry=Execute(F.Rect(100,true));TestTrue(*Dry.Message,Dry.bSuccess);
    TestEqual(TEXT("outside foliage verdict"),Dry.Foliage.Verdict,FString(TEXT("NO_FOLIAGE")));TestEqual(TEXT("dont snap"),Dry.Foliage.SnappingMode,FString(TEXT("DontSnap")));
    const int32 Undo=UToolsetLibrary::GetActiveUndoCount();
    const auto O=Execute(F.Rect());FString Json;FJsonObjectConverter::UStructToJsonObjectString(O,Json);AddInfo(TEXT("APPLY ")+Json.Left(4000));
    if(!TestEqual(*O.Message,O.Status,FString(TEXT("APPLIED"))))return false;
    TestTrue(TEXT("deferred update flushed and checked"),O.Foliage.PostApply.bDeferredUpdateChecked);
    TestTrue(TEXT("instances unchanged"),O.Foliage.PostApply.bInstancesUnchanged&&SameTransforms(Instances,F.Instances()));
    TestEqual(TEXT("IFA bytes unchanged"),Hash(F.IFA),IFAHash);TestEqual(TEXT("one transaction"),UToolsetLibrary::GetActiveUndoCount(),Undo+1);
    const auto Applied=F.Read();
    // 遅延更新をもう一度流しても変わらない（Editorの次のtick相当）。
    F.Actor->ForceUpdateLayersContent();TestTrue(TEXT("deferred update keeps foliage"),SameTransforms(Instances,F.Instances()));
    TestTrue(TEXT("undo"),GEditor->UndoTransaction());F.Actor->ForceUpdateLayersContent();
    TestTrue(TEXT("terrain restored"),F.Read().Heights==Original.Heights);TestTrue(TEXT("foliage unchanged after undo"),SameTransforms(Instances,F.Instances()));
    TestTrue(TEXT("redo"),GEditor->RedoTransaction());F.Actor->ForceUpdateLayersContent();
    TestTrue(TEXT("terrain redone"),F.Read().Heights==Applied.Heights);TestTrue(TEXT("foliage unchanged after redo"),SameTransforms(Instances,F.Instances()));
    TestEqual(TEXT("IFA bytes unchanged after undo/redo"),Hash(F.IFA),IFAHash);TestEqual(TEXT("no saves"),F.Saves,0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05Blocked,"LandscapeMCP.V05.Foliage.ImpactBlocked",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05Blocked::RunTest(const FString&)
{
    FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
    F.Add(FVector(3000,3000,0));F.Add(FVector(3150,2900,0),false);F.Add(FVector(500,500,0));
    const auto Instances=F.Instances();const auto Original=F.Read();const uint32 IFAHash=Hash(F.IFA);
    for(double Target:{100.0,-100.0})
    {
        const auto Dry=Execute(F.Rect(Target,true));TestTrue(*Dry.Message,Dry.bSuccess);
        FString Json;FJsonObjectConverter::UStructToJsonObjectString(Dry.Foliage,Json);AddInfo(FString::Printf(TEXT("DRYRUN target=%.0f foliage=%s"),Target,*Json));
        TestEqual(TEXT("blocked"),Dry.Foliage.Verdict,FString(TEXT("BLOCKED")));
        TestEqual(TEXT("affected instances"),Target>0?Dry.Foliage.BuriedCount:Dry.Foliage.FloatingCount,2);
        TestEqual(TEXT("predicted amount"),Target>0?Dry.Foliage.MaxBuryCm:Dry.Foliage.MaxFloatCm,100.0);
        TestTrue(TEXT("warning"),Dry.Warnings.ContainsByPredicate([](const FString& W){return W.StartsWith(TEXT("FOLIAGE_BLOCKED"));}));
        const int32 Undo=UToolsetLibrary::GetActiveUndoCount();FString Stage;
        const auto O=Execute(F.Rect(Target),&Stage);
        TestEqual(TEXT("refused"),O.ErrorCode,FString(TEXT("FOLIAGE_SAFETY_CONFLICT")));TestEqual(TEXT("stage"),Stage,FString(TEXT("foliageGate")));
        TestTrue(TEXT("impact reason"),O.Message.Contains(TEXT("FOLIAGE_IMPACT: ")));
        TestEqual(TEXT("apply gate equals dry run"),O.Foliage.BuriedCount+O.Foliage.FloatingCount,Dry.Foliage.BuriedCount+Dry.Foliage.FloatingCount);
        TestEqual(TEXT("no transaction"),UToolsetLibrary::GetActiveUndoCount(),Undo);
        TestTrue(TEXT("terrain unchanged"),F.Read().Heights==Original.Heights);TestTrue(TEXT("foliage unchanged"),SameTransforms(Instances,F.Instances()));
    }
    TestEqual(TEXT("IFA bytes unchanged"),Hash(F.IFA),IFAHash);TestEqual(TEXT("no saves"),F.Saves,0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05Boundary,"LandscapeMCP.V05.Foliage.BoundaryStraddle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05Boundary::RunTest(const FString&)
{
    // 2x2 component（境界はx=6300 / y=6300）。境界をまたぐ編集と、境界上のinstance。
    FPreviewRequest R;
    {
    FFixture F(2);if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
    R=F.Rect();R.Center=FVector2D(6300,3000);R.SizeX=1000;R.SizeY=600;R.Falloff=200;
    F.Add(FVector(6300,6300,0));F.Add(FVector(6300,9000,0));   // 編集の外、component境界上
    const auto Instances=F.Instances();
    auto Dry=R;Dry.bDryRun=true;const auto D=Execute(Dry);TestTrue(*D.Message,D.bSuccess);
    TestEqual(TEXT("boundary instances outside the edit"),D.Foliage.Verdict,FString(TEXT("NO_FOLIAGE")));
    const auto O=Execute(R);TestEqual(*O.Message,O.Status,FString(TEXT("APPLIED")));TestTrue(TEXT("unchanged"),SameTransforms(Instances,F.Instances()));
    // 編集の中のcomponent境界上（x=6300）にinstanceを足すと拒否。
    F.Add(FVector(6300,3000,0));
    FPreviewRequest R2=R;R2.TargetHeight=-50;const auto B=Execute(R2);
    TestEqual(TEXT("instance on component seam inside the edit"),B.ErrorCode,FString(TEXT("FOLIAGE_SAFETY_CONFLICT")));
    }
    // 影響範囲（falloffの外端）の外側のhalo内のinstanceは変化なし。
    FFixture G(2);if(!TestTrue(TEXT("fixture 2"),G.Valid()))return false;
    const FVector2D HaloXY(6300+500+200+50,3000);G.Add(FVector(HaloXY.X,HaloXY.Y,0));
    FPreviewRequest R3=R;R3.LandscapePath=G.Path;R3.bDryRun=true;const auto H=Execute(R3);
    TestTrue(*H.Message,H.bSuccess);TestEqual(TEXT("halo instance safe"),H.Foliage.Verdict,FString(TEXT("SAFE_TO_APPLY")));TestEqual(TEXT("inspected"),H.Foliage.InspectedCount,1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05Large,"LandscapeMCP.V05.Foliage.LargeCount",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05Large::RunTest(const FString&)
{
    double DryMs=0,ApplyMs=0;
    {
    FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
    // 20,000 instanceを編集範囲の外（x<1800）に置く。
    for(int32 I=0;I<20000;++I)F.Add(FVector(50+(I%100)*17.0,50+(I/100)*31.0,0));
    const double Start=FPlatformTime::Seconds();const auto Dry=Execute(F.Rect(100,true));DryMs=(FPlatformTime::Seconds()-Start)*1000;
    TestTrue(*Dry.Message,Dry.bSuccess);TestEqual(TEXT("far instances"),Dry.Foliage.Verdict,FString(TEXT("NO_FOLIAGE")));
    const double ApplyStart=FPlatformTime::Seconds();const auto O=Execute(F.Rect());ApplyMs=(FPlatformTime::Seconds()-ApplyStart)*1000;
    TestEqual(*O.Message,O.Status,FString(TEXT("APPLIED")));TestTrue(TEXT("unchanged"),O.Foliage.PostApply.bInstancesUnchanged);
    }
    // 走査上限ちょうど・超過はV05.Foliage.ScanLimitExactで確認する。
    AddInfo(FString::Printf(TEXT("PERFORMANCE foliage20000 dryRunMs=%.3f applyMs=%.3f"),DryMs,ApplyMs));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05Snap,"LandscapeMCP.V05.Foliage.SnapModeRefused",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05Snap::RunTest(const FString&)
{
    {
    FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
    F.Add(FVector(500,500,0));const auto Instances=F.Instances();const auto Original=F.Read();
    for(EFoliageLandscapeSnappingMode Mode:{EFoliageLandscapeSnappingMode::SnapUnderBrushOnly,EFoliageLandscapeSnappingMode::SnapEverywhere})
    {
        FScopedSnapMode Scope(*this,Mode);if(!TestTrue(TEXT("snap mode fixture"),Scope.Valid()))return false;
        const auto Dry=Execute(F.Rect(100,true));TestEqual(TEXT("snap blocked"),Dry.Foliage.Verdict,FString(TEXT("BLOCKED")));
        FString Stage;const auto O=Execute(F.Rect(),&Stage);
        TestEqual(TEXT("refused"),O.ErrorCode,FString(TEXT("FOLIAGE_SAFETY_CONFLICT")));TestTrue(TEXT("snap reason"),O.Message.Contains(TEXT("FOLIAGE_SNAP_MODE: ")));
        TestTrue(TEXT("terrain unchanged"),F.Read().Heights==Original.Heights);TestTrue(TEXT("foliage unchanged"),SameTransforms(Instances,F.Instances()));
        TestEqual(TEXT("capabilities report the mode"),Capabilities().FoliageSnappingMode,FString(Mode==EFoliageLandscapeSnappingMode::SnapEverywhere?TEXT("SnapEverywhere"):TEXT("SnapUnderBrushOnly")));
    }
    }
    TestEqual(TEXT("project setting back to DontSnap"),Capabilities().FoliageSnappingMode,FString(TEXT("DontSnap")));
    // 付属のないinstanceだけなら、snap modeに関係なくUEは動かさない。
    FFixture G;if(!TestTrue(TEXT("fixture 2"),G.Valid()))return false;G.Add(FVector(500,500,0),false);
    {
        FScopedSnapMode Scope(*this,EFoliageLandscapeSnappingMode::SnapEverywhere);
        TestEqual(TEXT("unattached only applies"),Execute(G.Rect()).Status,FString(TEXT("APPLIED")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05Unknown,"LandscapeMCP.V05.Foliage.UnknownForms",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05Unknown::RunTest(const FString&)
{
    {
        // Procedural Foliageが作ったinstance（ProceduralGuidあり）。
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
        FFoliageInstance I;I.Location=FVector(3000,3000,0);I.ProceduralGuid=FGuid::NewGuid();F.Info->AddInstance(F.Type,I,nullptr);
        const auto Dry=Execute(F.Rect(0,true));TestEqual(TEXT("procedural instance unknown"),Dry.Foliage.Verdict,FString(TEXT("UNKNOWN")));
        const auto O=Execute(F.Rect());TestEqual(TEXT("refused"),O.ErrorCode,FString(TEXT("FOLIAGE_SAFETY_CONFLICT")));TestTrue(TEXT("unknown reason"),O.Message.Contains(TEXT("FOLIAGE_UNKNOWN: ")));
    }
    {
        // Procedural Foliage componentを持つActorが範囲に重なる。
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
        AActor* Volume=F.World->SpawnActor<AActor>(FVector(3000,3000,0),FRotator::ZeroRotator);
        UProceduralFoliageComponent* PFC=NewObject<UProceduralFoliageComponent>(Volume);Volume->AddInstanceComponent(PFC);PFC->RegisterComponent();
        const auto Dry=Execute(F.Rect(100,true));TestEqual(TEXT("procedural component unknown"),Dry.Foliage.Verdict,FString(TEXT("UNKNOWN")));
        TestTrue(TEXT("procedural unknown listed"),Dry.Foliage.Unknowns.ContainsByPredicate([](const FString& U){return U.StartsWith(TEXT("PROCEDURAL_FOLIAGE: "));}));
    }
    {
        // Landscape Grass（instance単位で追えない）。Applyはv0.4どおりgrassの検査で拒否。
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
        F.Actor->FoliageComponents.Add(NewObject<UHierarchicalInstancedStaticMeshComponent>(F.Actor));
        const auto Dry=Execute(F.Rect(100,true));TestEqual(TEXT("grass unknown"),Dry.Foliage.Verdict,FString(TEXT("UNKNOWN")));
        TestEqual(TEXT("grass apply refused"),Execute(F.Rect()).ErrorCode,FString(TEXT("UNSUPPORTED_GRASS_STATE")));
        F.Actor->FoliageComponents.Reset();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05ReadOnly,"LandscapeMCP.V05.Foliage.ReadOnlyTools",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05ReadOnly::RunTest(const FString&)
{
    FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
    F.Add(FVector(3000,3000,0));F.Add(FVector(3100,3000,0));F.Add(FVector(1000,1000,0),false);
    UPackage* Package=F.World->GetOutermost();Package->SetDirtyFlag(false);
    const int32 Undo=UToolsetLibrary::GetActiveUndoCount();const uint32 IFAHash=Hash(F.IFA);const auto Original=F.Read();const auto Instances=F.Instances();
    TestTrue(TEXT("GetHeight"),Height(F.Path,3000,3000).bSuccess);
    TestTrue(TEXT("GetSlope"),Slope(F.Path,3000,3000,100).bSuccess);
    TestTrue(TEXT("GetHeightRegion"),HeightRegion(F.Path,2000,2000,4000,4000,100).bSuccess);
    TestTrue(TEXT("EvaluateWalkability"),Walkability(F.Path,3000,3000,44.77,100).bSuccess);
    TestTrue(TEXT("AnalyzeSlopeNeighborhood"),SlopeNeighborhood(F.Path,3000,3000,300,100).bSuccess);
    TestTrue(TEXT("EvaluateWalkabilityRegion"),WalkabilityRegion(F.Path,2000,2000,4000,4000,44.77).bSuccess);
    TestTrue(TEXT("InspectFoliage"),FoliageInspect(F.Path,0,0,6300,6300,256).bSuccess);
    TestTrue(TEXT("GetCapabilities"),Capabilities().bSuccess);
    TestTrue(TEXT("Dry-run"),Execute(F.Rect(100,true)).bSuccess);
    TestFalse(TEXT("no package dirty"),Package->IsDirty());TestEqual(TEXT("no undo entries"),UToolsetLibrary::GetActiveUndoCount(),Undo);
    TestEqual(TEXT("IFA bytes unchanged"),Hash(F.IFA),IFAHash);TestTrue(TEXT("instances unchanged"),SameTransforms(Instances,F.Instances()));
    TestTrue(TEXT("terrain unchanged"),F.Read().Heights==Original.Heights);TestFalse(TEXT("no active transaction"),GEditor->IsTransactionActive());
    // 円形ブラシの書き込みはv0.4どおり拒否（事後検証がないため）。
    FEditRequest E;E.LandscapePath=F.Path;E.Center=FVector2D(3000,3000);E.RadiusCm=300;E.Strength=100;E.bDryRun=false;
    TestTrue(TEXT("circular brush write still refused"),Edit(E).Message.Contains(TEXT("Attached foliage is unsupported")));
    TestEqual(TEXT("no saves"),F.Saves,0);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05Inspect,"LandscapeMCP.V05.Foliage.Inspect",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05Inspect::RunTest(const FString&)
{
    FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
    F.Add(FVector(3000,3000,20));F.Add(FVector(3100,3000,0));F.Add(FVector(3200,3000,0),false);F.Add(FVector(500,500,0));
    auto R=FoliageInspect(F.Path,2500,2500,3500,3500,256);
    if(!TestTrue(*R.Message,R.bSuccess))return false;
    TestEqual(TEXT("status"),R.Status,FString(TEXT("INSPECTED")));TestEqual(TEXT("count in region"),R.InstanceCount,3);
    TestEqual(TEXT("one type"),R.Types.Num(),1);
    if(R.Types.Num()==1){TestEqual(TEXT("attached"),R.Types[0].TargetLandscapeCount,2);TestEqual(TEXT("unattached"),R.Types[0].UnattachedCount,1);}
    const auto* Raised=R.Instances.FindByPredicate([](const FLandscapeMCPFoliageInstanceInfo& I){return I.Location.Z==20;});
    if(TestNotNull(TEXT("raised instance"),Raised)){TestEqual(TEXT("terrain"),Raised->TerrainHeightCm,0.0);TestEqual(TEXT("height above terrain"),Raised->HeightAboveTerrainCm,20.0);}
    TestTrue(TEXT("attached flag"),R.bTargetHasAttachedFoliage);TestEqual(TEXT("snap mode"),R.SnappingMode,FString(TEXT("DontSnap")));
    R=FoliageInspect(F.Path,2500,2500,3500,3500,1);TestTrue(TEXT("truncated"),R.bTruncated);TestEqual(TEXT("returned one"),R.Instances.Num(),1);TestEqual(TEXT("count unaffected"),R.InstanceCount,3);
    TestEqual(TEXT("empty region"),FoliageInspect(F.Path,4000,4000,5000,5000,256).Status,FString(TEXT("NO_FOLIAGE")));
    TestFalse(TEXT("reversed"),FoliageInspect(F.Path,3500,3500,2500,2500,256).bSuccess);
    TestFalse(TEXT("NaN"),FoliageInspect(F.Path,NAN,0,1,1,256).bSuccess);
    TestFalse(TEXT("outside landscape"),FoliageInspect(F.Path,6000,6000,7000,7000,256).bSuccess);
    TestFalse(TEXT("max instances below range"),FoliageInspect(F.Path,0,0,1,1,-1).bSuccess);
    TestFalse(TEXT("max instances above range"),FoliageInspect(F.Path,0,0,1,1,4097).bSuccess);
    TestFalse(TEXT("name instead of path"),FoliageInspect(TEXT("V05FoliageFixture"),0,0,1,1,256).bSuccess);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05Capabilities,"LandscapeMCP.V05.Adapter.Capabilities",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05Capabilities::RunTest(const FString&)
{
    const auto C=Capabilities();
    TestTrue(TEXT("success without a landscape"),C.bSuccess);TestEqual(TEXT("engine"),C.Engine,FString(TEXT("UnrealEngine")));
    TestTrue(TEXT("engine 5.8"),C.EngineVersion.StartsWith(TEXT("5.8")));TestEqual(TEXT("adapter"),C.Adapter,FString(TEXT("Unreal")));
    TestEqual(TEXT("toolset version"),C.ToolsetVersion,GetDefault<ULandscapeMCPToolset>()->GetToolsetVersion());
    TestFalse(TEXT("build id"),C.BuildId.IsEmpty());TestEqual(TEXT("foliage apply"),C.FoliageApply,FString(TEXT("PRESERVE_IN_PLACE_ONLY")));
    TestEqual(TEXT("world partition"),C.WorldPartition,FString(TEXT("UNSUPPORTED")));TestEqual(TEXT("latch"),C.bMutationDisabled,MutationDisabled());
    // descriptorのVersionNameと一致すること（中間Buildの検出）。
    FString Text;TSharedPtr<FJsonObject> Descriptor;
    const FString File=FPaths::ProjectPluginsDir()/TEXT("LandscapeMCP/LandscapeMCP.uplugin");
    if(TestTrue(TEXT("descriptor"),FFileHelper::LoadFileToString(Text,*File)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Descriptor)&&Descriptor.IsValid()))
        TestEqual(TEXT("plugin version name"),C.PluginVersionName,Descriptor->GetStringField(TEXT("VersionName")));
    TestTrue(TEXT("same adapter instance"),&Engine::GetActiveAdapter()==&Engine::GetActiveAdapter());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05Schema,"LandscapeMCP.V05.Schema.AdditiveOnly",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05Schema::RunTest(const FString&)
{
    const FString Schema=UToolsetRegistry::GetToolsetJsonSchema(ULandscapeMCPToolset::StaticClass());
    TSharedPtr<FJsonObject> Current;if(!TestTrue(TEXT("schema JSON"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Schema),Current)&&Current.IsValid()))return false;
    TSet<FString> Names;for(const auto& Tool:Current->GetArrayField(TEXT("tools")))Names.Add(Tool->AsObject()->GetStringField(TEXT("name")).RightChop(Tool->AsObject()->GetStringField(TEXT("name")).Find(TEXT("."),ESearchCase::CaseSensitive,ESearchDir::FromEnd)+1));
    for(const TCHAR* Name:{TEXT("GetCapabilities"),TEXT("InspectFoliage"),TEXT("FlattenRectangle"),TEXT("FlattenPath"),TEXT("CreateWalkablePath"),TEXT("GetHeight")})TestTrue(Name,Names.Contains(Name));
    for(const auto& Tool:Current->GetArrayField(TEXT("tools")))
    {
        const auto Obj=Tool->AsObject();const FString N=Obj->GetStringField(TEXT("name"));
        if(!N.EndsWith(TEXT("FlattenRectangle"))&&!N.EndsWith(TEXT("FlattenPath"))&&!N.EndsWith(TEXT("CreateWalkablePath")))continue;
        const auto Out=Obj->GetObjectField(TEXT("outputSchema"))->GetObjectField(TEXT("properties"))->GetObjectField(TEXT("returnValue"))->GetObjectField(TEXT("properties"));
        TestTrue(TEXT("foliage output added"),Out->HasField(TEXT("foliage")));
        for(const TCHAR* Field:{TEXT("status"),TEXT("errorCode"),TEXT("plannedModifiedCount"),TEXT("affectedBounds"),TEXT("warnings"),TEXT("rollbackStatus")})TestTrue(Field,Out->HasField(Field));
    }
    AddInfo(Schema.Left(200));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05Rollback,"LandscapeMCP.V05.Foliage.RollbackFault",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05Rollback::RunTest(const FString&)
{
    for(EApplyFault Fault:{EApplyFault::SourceMismatch,EApplyFault::CollisionFailure,EApplyFault::RestoreMismatch})
    {
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
        F.Add(FVector(500,500,0));F.Add(FVector(6000,6000,0),false);const auto Instances=F.Instances();const uint32 IFAHash=Hash(F.IFA);const auto Original=F.Read();
        SetAutomationApplyFault(Fault);const auto O=Execute(F.Rect());SetAutomationApplyFault(EApplyFault::None);
        AddInfo(FString::Printf(TEXT("FAULT status=%s rollback=%s latch=%d"),*O.Status,*O.RollbackStatus,MutationDisabled()));
        TestFalse(TEXT("never success"),O.bSuccess);
        if(Fault==EApplyFault::RestoreMismatch){TestEqual(TEXT("rollback failed"),O.Status,FString(TEXT("ROLLBACK_FAILED")));TestTrue(TEXT("latch"),MutationDisabled());}
        else{TestEqual(TEXT("rolled back"),O.Status,FString(TEXT("ROLLED_BACK")));TestEqual(TEXT("verified"),O.RollbackStatus,FString(TEXT("VERIFIED")));TestTrue(TEXT("terrain restored"),F.Read().Heights==Original.Heights);}
        TestTrue(TEXT("foliage never changed"),SameTransforms(Instances,F.Instances()));TestEqual(TEXT("IFA bytes unchanged"),Hash(F.IFA),IFAHash);
        TestEqual(TEXT("no saves"),F.Saves,0);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// v0.5 Focused Fix（Codex独立レビュー B1 / M1 / M2 / M3 / N1 / N2）
// ---------------------------------------------------------------------------------------------
namespace LandscapeMCP::FoliageTests
{
    // 3x3頂点（Rect=Extent=(0,0)-(2,2)）、1m間隔、Scale.Z=100（1 step = 0.78125 cm）。
    Authoring::FHeightfieldSnapshot Grid3(){return Flat(3);}
    int32 Idx(int32 X,int32 Y){return Y*3+X;}
    uint16 Cm(double H){return uint16(32768+FMath::RoundToInt(H*128.0/100.0));}
    // 対象Landscapeの頂点高さをテスト用に直接書く（Transactionなし、/Tempの未保存Mapだけ）。
    bool WriteVertices(FFixture& F,const TMap<FIntPoint,double>& Cms)
    {
        ULandscapeInfo* Info=F.Actor->GetLandscapeInfo();FIntRect Extent;
        if(!Info||!Info->GetLandscapeExtent(Extent)||!F.Actor->GetEditLayer(0))return false;
        const FGuid Layer=F.Actor->GetEditLayer(0)->GetGuid();
        TArray<uint16> Data;Data.Init(32768,(Extent.Width()+1)*(Extent.Height()+1));
        for(const auto& Pair:Cms)Data[(Pair.Key.Y-Extent.Min.Y)*(Extent.Width()+1)+Pair.Key.X-Extent.Min.X]=Cm(Pair.Value);
        {
            FScopedSetLandscapeEditingLayer Scope(F.Actor,Layer);FLandscapeEditDataInterface Edit(Info,Layer);
            Edit.SetHeightData(Extent.Min.X,Extent.Min.Y,Extent.Max.X,Extent.Max.Y,Data.GetData(),0,false,nullptr,nullptr,nullptr,false,nullptr,nullptr,true,true,true);Edit.Flush();
        }
        F.Actor->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Heightmap_All);F.Actor->ForceUpdateLayersContent();return true;
    }
    // Landscapeだけに当てる（Foliageのmeshは無視）。
    bool Trace(UWorld* W,FVector2D XY,double& Z,const AActor* Ignore=nullptr)
    {
        FHitResult Hit;FCollisionQueryParams Q(FName(TEXT("LandscapeMCPV05Triangle")),true);if(Ignore)Q.AddIgnoredActor(Ignore);
        if(!W->LineTraceSingleByChannel(Hit,FVector(XY.X,XY.Y,5000),FVector(XY.X,XY.Y,-5000),ECC_WorldStatic,Q))return false;
        Z=Hit.ImpactPoint.Z;return true;
    }
    // テスト内だけのsublevel。保存しない。Worldの外で作ったLevelをLevelsへ足し、終わりに外して破棄する。
    struct FScopedSubLevel
    {
        UWorld* Owner=nullptr;UWorld* Sub=nullptr;ULevel* Level=nullptr;AInstancedFoliageActor* IFA=nullptr;UFoliageType* Type=nullptr;FFoliageInfo* Info=nullptr;
        explicit FScopedSubLevel(UWorld* InOwner):Owner(InOwner)
        {
            Sub=UWorld::CreateWorld(EWorldType::Inactive,false,TEXT("V05SubLevel"),nullptr,true);if(!Sub)return;
            Level=Sub->PersistentLevel;Owner->AddLevel(Level);
            IFA=AInstancedFoliageActor::GetInstancedFoliageActorForLevel(Level,true);
            if(UStaticMesh* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));IFA&&Mesh)Info=IFA->AddMesh(Mesh,&Type);
        }
        bool Valid()const{return Level&&IFA&&Info&&Type;}
        void Add(FVector L){FFoliageInstance I;I.Location=L;Info->AddInstance(Type,I,nullptr);}
        ~FScopedSubLevel()
        {
            if(Level){Owner->RemoveLevel(Level);}
            if(Sub){Sub->DestroyWorld(false);Sub->RemoveFromRoot();Sub->MarkAsGarbage();}
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05Triangle,"LandscapeMCP.V05.Foliage.TriangleHeight",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05Triangle::RunTest(const FString&)
{
    const auto S=Grid3();const double Q=100.0/128.0;
    Foliage::FInventory Inv;Inv.SnappingMode=TEXT("DontSnap");Inv.bSnappingSafe=true;
    auto At=[&](TConstArrayView<uint16> H,FVector2D P){double Z=0,Slope=0;TestTrue(TEXT("readable"),Foliage::TerrainAt(S,H,P,Z,Slope));return Z;};
    auto Bilinear=[&](TConstArrayView<uint16> H,FVector2D P){double Z=0;S.Sample(P,H,Z);return Z;};
    // (1) Codexの数値反例。点(50,25)はLower三角形(00,10,11)の中。
    {
        TArray<uint16> Before=S.Heights,After=S.Heights;
        Before[Idx(1,1)]=Cm(150);After[Idx(0,1)]=Cm(100);After[Idx(1,1)]=Cm(50);
        const FVector2D P(50,25);
        TestEqual(TEXT("bilinear before (old definition)"),Bilinear(Before,P),18.75);TestEqual(TEXT("bilinear after (old definition)"),Bilinear(After,P),18.75);
        TestEqual(TEXT("triangle before"),At(Before,P),37.5);TestEqual(TEXT("triangle after"),At(After,P),12.5);
        auto Snap=S;Snap.Heights=Before;Inv.Samples={Sample(FVector(50,25,37.5))};
        const auto O=Foliage::Evaluate(Snap,After,Inv);
        TestEqual(TEXT("counterexample floating"),O.FloatingCount,1);TestEqual(TEXT("counterexample amount"),O.MaxFloatCm,25.0);
        TestEqual(TEXT("counterexample blocked"),O.Verdict,FString(TEXT("BLOCKED")));
        if(O.Samples.Num()==1){TestEqual(TEXT("reported delta"),O.Samples[0].DeltaCm,-25.0);TestEqual(TEXT("reported current"),O.Samples[0].CurrentTerrainCm,37.5);}
    }
    // (2) bilinearも傾斜も変わらず、実三角形の高さだけが変わる例（修正前はUNCHANGED / SAFE_TO_APPLYだった）。両方向・両三角形。
    struct FCase{const TCHAR* Name;FVector2D P;FIntPoint Shifted[3];FIntPoint Opposite;double Shift;const TCHAR* Expected;};
    const FCase Cases[]={
        {TEXT("lower +25"),FVector2D(50,25),{FIntPoint(0,0),FIntPoint(1,0),FIntPoint(1,1)},FIntPoint(0,1),25,TEXT("BURIED")},
        {TEXT("lower -25"),FVector2D(50,25),{FIntPoint(0,0),FIntPoint(1,0),FIntPoint(1,1)},FIntPoint(0,1),-25,TEXT("FLOATING")},
        {TEXT("upper +25"),FVector2D(25,50),{FIntPoint(0,0),FIntPoint(0,1),FIntPoint(1,1)},FIntPoint(1,0),25,TEXT("BURIED")},
        {TEXT("upper -25"),FVector2D(25,50),{FIntPoint(0,0),FIntPoint(0,1),FIntPoint(1,1)},FIntPoint(1,0),-25,TEXT("FLOATING")}};
    for(const FCase& C:Cases)
    {
        TArray<uint16> After=S.Heights;for(const FIntPoint& V:C.Shifted)After[Idx(V.X,V.Y)]=Cm(C.Shift);After[Idx(C.Opposite.X,C.Opposite.Y)]=Cm(-7*C.Shift);
        TestEqual(FString(C.Name)+TEXT(": bilinear unchanged"),Bilinear(After,C.P),0.0,1.e-9);
        Foliage::FTerrainChange Change;TestTrue(C.Name,Foliage::ChangeAt(S,S.Heights,After,C.P,Change));
        TestEqual(FString(C.Name)+TEXT(": triangle slope unchanged"),Change.SlopeChangeDegrees,0.0,1.e-9);TestEqual(FString(C.Name)+TEXT(": delta"),Change.DeltaCm,C.Shift);
        Inv.Samples={Sample(FVector(C.P.X,C.P.Y,0))};const auto O=Foliage::Evaluate(S,After,Inv);
        TestEqual(FString(C.Name)+TEXT(": verdict"),O.Verdict,FString(TEXT("BLOCKED")));
        if(TestEqual(FString(C.Name)+TEXT(": one sample"),O.Samples.Num(),1))TestEqual(FString(C.Name)+TEXT(": class"),O.Samples[0].Classification,FString(C.Expected));
    }
    // (3) 対角線上: 高さは(H00+H11)/2でH10に依らない。傾斜はLowerだけ変わる → 接する三角形の最大でSLOPE_CHANGED。
    {
        TArray<uint16> After=S.Heights;After[Idx(1,0)]=Cm(50);
        Foliage::FTerrainChange Change;TestTrue(TEXT("diagonal"),Foliage::ChangeAt(S,S.Heights,After,FVector2D(50,50),Change));
        TestEqual(TEXT("diagonal height unchanged"),Change.DeltaCm,0.0);TestTrue(TEXT("diagonal slope uses the changed triangle"),Change.SlopeChangeDegrees>20);
        Inv.Samples={Sample(FVector(50,50,0))};TestEqual(TEXT("diagonal class"),Foliage::Evaluate(S,After,Inv).Samples[0].Classification,FString(TEXT("SLOPE_CHANGED")));
        // 対角線のわずかにUpper側: Upperだけが接するので傾斜は変わらない。
        TestTrue(TEXT("just above diagonal"),Foliage::ChangeAt(S,S.Heights,After,FVector2D(50,50.001),Change));TestEqual(TEXT("upper only"),Change.SlopeChangeDegrees,0.0);
    }
    // (4) 頂点上（内部頂点(1,1)）: 接する6三角形のどれかが変われば傾斜変化として数える。高さはその頂点の値。
    {
        TArray<uint16> After=S.Heights;After[Idx(2,1)]=Cm(30);
        Foliage::FTerrainChange Change;TestTrue(TEXT("vertex"),Foliage::ChangeAt(S,S.Heights,After,FVector2D(100,100),Change));
        TestEqual(TEXT("vertex height"),Change.DeltaCm,0.0);TestTrue(TEXT("vertex slope from a neighbouring cell"),Change.SlopeChangeDegrees>10);TestTrue(TEXT("complete"),Change.bComplete);
    }
    // (5) Snapshotの端（Landscapeの内側）: 外側の三角形が読めない。Snapshot内の境界頂点が変わるなら判定不能、変わらないなら評価できる。
    {
        auto Part=S;Part.Extent=FIntRect(0,0,10,10);   // Landscapeはもっと広い
        TArray<uint16> After=S.Heights;After[Idx(2,0)]=Cm(10);
        Foliage::FTerrainChange Change;TestTrue(TEXT("edge readable"),Foliage::ChangeAt(Part,S.Heights,After,FVector2D(200,50),Change));
        TestFalse(TEXT("edge vertex changed: outside triangle not provable"),Change.bComplete);
        Inv.Samples={Sample(FVector(200,50,0))};const auto O=Foliage::Evaluate(Part,After,Inv);
        TestEqual(TEXT("edge unknown"),O.Verdict,FString(TEXT("UNKNOWN")));if(O.Samples.Num()==1)TestEqual(TEXT("edge reason"),O.Samples[0].Reason,FString(TEXT("ADJACENT_TRIANGLE_OUTSIDE_READ_BOUNDS")));
        TArray<uint16> Inner=S.Heights;Inner[Idx(0,0)]=Cm(10);
        TestTrue(TEXT("edge unchanged readable"),Foliage::ChangeAt(Part,S.Heights,Inner,FVector2D(200,50),Change));TestTrue(TEXT("edge vertices unchanged: complete"),Change.bComplete);
        // Landscape自体の端（Extent==Rect）では外側に三角形がないので評価できる。
        TestTrue(TEXT("landscape edge"),Foliage::ChangeAt(S,S.Heights,After,FVector2D(200,50),Change));TestTrue(TEXT("landscape edge complete"),Change.bComplete);
        double Z=0,Slope=0;TestFalse(TEXT("outside snapshot"),Foliage::TerrainAt(S,S.Heights,FVector2D(200.001,50),Z,Slope));
        TestFalse(TEXT("NaN"),Foliage::TerrainAt(S,S.Heights,FVector2D(NAN,50),Z,Slope));TestFalse(TEXT("Infinity"),Foliage::TerrainAt(S,S.Heights,FVector2D(INFINITY,50),Z,Slope));
    }
    // (6) 量子化の閾値（半段 = Q/2）: Landscape端の辺の中点で1 step → |Δ| = Q/2（一致は変化なし）、2 step → Q（変化あり）、1/4点で1 step → Q/4。
    {
        TArray<uint16> One=S.Heights;One[Idx(1,0)]+=1;TArray<uint16> Two=S.Heights;Two[Idx(1,0)]+=2;
        Foliage::FTerrainChange Change;
        Foliage::ChangeAt(S,S.Heights,One,FVector2D(50,0),Change);TestEqual(TEXT("equal to half step"),Change.DeltaCm,Q/2);
        Inv.Samples={Sample(FVector(50,0,0))};auto O=Foliage::Evaluate(S,One,Inv);
        TestEqual(TEXT("half step is not buried"),O.BuriedCount,0);TestEqual(TEXT("half step: slope changes instead"),O.SlopeChangedCount,1);
        O=Foliage::Evaluate(S,Two,Inv);TestEqual(TEXT("above half step is buried"),O.BuriedCount,1);
        Foliage::ChangeAt(S,S.Heights,One,FVector2D(25,0),Change);TestEqual(TEXT("below half step"),Change.DeltaCm,Q/4);
        using Foliage::EClass;const double H=Q/2,T=Foliage::SlopeChangeThresholdDegrees,E=1.e-9;
        TestTrue(TEXT("height just below"),Foliage::Classify(H-E,0,H,false)==EClass::Unchanged);
        TestTrue(TEXT("height equal"),Foliage::Classify(H,0,H,false)==EClass::Unchanged);
        TestTrue(TEXT("height just above"),Foliage::Classify(H+E,0,H,false)==EClass::Buried);
        TestTrue(TEXT("height just above (down)"),Foliage::Classify(-H-E,0,H,false)==EClass::Floating);
        TestTrue(TEXT("slope just below"),Foliage::Classify(0,T-E,H,false)==EClass::Unchanged);
        TestTrue(TEXT("slope equal"),Foliage::Classify(0,T,H,false)==EClass::Unchanged);
        TestTrue(TEXT("slope just above"),Foliage::Classify(0,T+E,H,false)==EClass::SlopeChanged);
        TestTrue(TEXT("snap risk"),Foliage::Classify(0,0,H,true)==EClass::MayBeMoved);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05TriangleCollision,"LandscapeMCP.V05.Foliage.TriangleMatchesCollision",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05TriangleCollision::RunTest(const FString&)
{
    // 非平面セル(30,30): 00=0, 10=0, 01=+100, 11=+150。実Collision（Chaos）と、TerrainAt / bilinearを比べる。
    FFixture F;if(!TestTrue(TEXT("fixture"),F.Actor!=nullptr))return false;
    if(!TestTrue(TEXT("write"),WriteVertices(F,{{FIntPoint(30,31),100},{FIntPoint(31,31),150}})))return false;
    const auto S=F.Read(FVector2D(2900,2900),FVector2D(3200,3200));
    const FVector2D Points[]={FVector2D(3050,3025),FVector2D(3025,3050),FVector2D(3050,3050),FVector2D(3075,3010),FVector2D(3010,3090),FVector2D(3100,3050),FVector2D(3050,3100),FVector2D(3000,3000)};
    double MaxTriangle=0,MaxBilinear=0;
    for(const FVector2D& P:Points)
    {
        double Z=0,Slope=0,Collision=0,Bi=0;
        if(!TestTrue(TEXT("terrain"),Foliage::TerrainAt(S,S.Heights,P,Z,Slope))||!TestTrue(TEXT("trace"),Trace(F.World,P,Collision,F.IFA)))continue;
        S.Sample(P,S.Heights,Bi);MaxTriangle=FMath::Max(MaxTriangle,FMath::Abs(Z-Collision));MaxBilinear=FMath::Max(MaxBilinear,FMath::Abs(Bi-Collision));
        AddInfo(FString::Printf(TEXT("COLLISION_MATCH x=%.1f y=%.1f triangle=%.4f collision=%.4f bilinear=%.4f"),P.X,P.Y,Z,Collision,Bi));
    }
    TestTrue(TEXT("triangle height equals native collision"),MaxTriangle<0.05);
    TestTrue(TEXT("bilinear differs from collision in the non-planar cell (the test discriminates)"),MaxBilinear>10);
    AddInfo(FString::Printf(TEXT("COLLISION_MATCH maxTriangleErrorCm=%.5f maxBilinearErrorCm=%.3f"),MaxTriangle,MaxBilinear));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05TriangleFlatten,"LandscapeMCP.V05.Foliage.TriangleFlattenRefused",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05TriangleFlatten::RunTest(const FString&)
{
    // 実際のFlattenRectangle計画（目標0、coreが2500..3500 x 2600..3400）。セル(30,30)のLower三角形を+/-25cmにし、
    // 頂点01を逆向きに7倍にする。instance(3050,3025)のbilinear高度は0のままで、三角形の傾斜も0のまま（修正前は見逃した）。
    for(const double Shift:{25.0,-25.0})
    {
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
        if(!TestTrue(TEXT("write"),WriteVertices(F,{{FIntPoint(30,30),Shift},{FIntPoint(31,30),Shift},{FIntPoint(31,31),Shift},{FIntPoint(30,31),-7*Shift}})))return false;
        const FVector2D XY(3050,3025);double Collision=0;
        if(TestTrue(TEXT("trace"),Trace(F.World,XY,Collision,F.IFA)))TestEqual(TEXT("instance stands on the real triangle"),Collision,Shift,0.05);
        F.Add(FVector(XY.X,XY.Y,Shift));
        const auto Original=F.Read();const auto Instances=F.Instances();const uint32 IFAHash=Hash(F.IFA);
        FPreviewRequest R=F.Rect(0);
        double Bi=0;Original.Sample(XY,Original.Heights,Bi);TestEqual(TEXT("bilinear height is the flatten target already"),Bi,0.0,1.e-6);
        const auto Dry=Execute(F.Rect(0,true));TestTrue(*Dry.Message,Dry.bSuccess);
        FString Json;FJsonObjectConverter::UStructToJsonObjectString(Dry.Foliage,Json);AddInfo(FString::Printf(TEXT("TRIANGLE_FLATTEN shift=%.0f foliage=%s"),Shift,*Json));
        TestEqual(TEXT("blocked"),Dry.Foliage.Verdict,FString(TEXT("BLOCKED")));
        TestEqual(TEXT("classified by height"),Shift>0?Dry.Foliage.FloatingCount:Dry.Foliage.BuriedCount,1);
        TestEqual(TEXT("amount"),Shift>0?Dry.Foliage.MaxFloatCm:Dry.Foliage.MaxBuryCm,25.0);
        const int32 Undo=UToolsetLibrary::GetActiveUndoCount();FString Stage;
        const auto O=Execute(R,&Stage);
        TestEqual(TEXT("refused"),O.ErrorCode,FString(TEXT("FOLIAGE_SAFETY_CONFLICT")));TestEqual(TEXT("stage"),Stage,FString(TEXT("foliageGate")));
        TestEqual(TEXT("no write"),O.ModifiedCount,0);TestEqual(TEXT("no transaction"),UToolsetLibrary::GetActiveUndoCount(),Undo);
        TestTrue(TEXT("terrain unchanged"),F.Read().Heights==Original.Heights);TestTrue(TEXT("foliage unchanged"),SameTransforms(Instances,F.Instances()));
        TestEqual(TEXT("IFA bytes unchanged"),Hash(F.IFA),IFAHash);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05OtherLevels,"LandscapeMCP.V05.Foliage.OtherLevels",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05OtherLevels::RunTest(const FString&)
{
    struct FMode{const TCHAR* Name;bool bVisible;bool bLocked;};
    for(const FMode& M:{FMode{TEXT("visible"),true,false},FMode{TEXT("hidden"),false,false},FMode{TEXT("locked"),true,true},FMode{TEXT("hidden+locked"),false,true}})
    {
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
        FScopedSubLevel Sub(F.World);if(!TestTrue(TEXT("sublevel"),Sub.Valid()))return false;
        Sub.Add(FVector(3000,3000,0));
        Sub.Level->bIsVisible=M.bVisible;
#if WITH_EDITORONLY_DATA
        Sub.Level->bLocked=M.bLocked;
#endif
        // 前提の確認: 既定のTActorIteratorは非表示Levelを見ない（修正前の列挙はこれを使っていた）。
        bool bDefaultIteratorSees=false;for(TActorIterator<AInstancedFoliageActor> It(F.World);It;++It)bDefaultIteratorSees|=*It==Sub.IFA;
        TestTrue(FString(M.Name)+TEXT(": default iterator sees only visible levels"),bDefaultIteratorSees==M.bVisible);
        const auto Original=F.Read();const int32 Levels=F.World->GetLevels().Num();const bool bSubDirty=Sub.Level->GetOutermost()->IsDirty();
        const auto Dry=Execute(F.Rect(100,true));
        TestEqual(FString(M.Name)+TEXT(": unknown"),Dry.Foliage.Verdict,FString(TEXT("UNKNOWN")));
        TestTrue(FString(M.Name)+TEXT(": other level reason"),Dry.Foliage.Samples.ContainsByPredicate([](const FLandscapeMCPFoliageImpactSample& S){return S.Reason==TEXT("FOLIAGE_IN_OTHER_LEVEL");}));
        const auto Inspect=FoliageInspect(F.Path,2500,2500,3500,3500,16);
        TestEqual(FString(M.Name)+TEXT(": inspect sees it"),Inspect.InstanceCount,1);TestEqual(FString(M.Name)+TEXT(": inspect status"),Inspect.Status,FString(TEXT("UNKNOWN_PRESENT")));
        const auto O=Execute(F.Rect());TestEqual(FString(M.Name)+TEXT(": refused"),O.ErrorCode,FString(TEXT("FOLIAGE_SAFETY_CONFLICT")));
        TestTrue(FString(M.Name)+TEXT(": terrain unchanged"),F.Read().Heights==Original.Heights);
        // Levelの状態を変えない（読み込み・表示・ロック・Level数）。
        TestTrue(FString(M.Name)+TEXT(": visibility unchanged"),bool(Sub.Level->bIsVisible)==M.bVisible);
#if WITH_EDITORONLY_DATA
        TestTrue(FString(M.Name)+TEXT(": lock unchanged"),bool(Sub.Level->bLocked)==M.bLocked);
#endif
        TestEqual(FString(M.Name)+TEXT(": no level loaded or removed"),F.World->GetLevels().Num(),Levels);
        TestTrue(FString(M.Name)+TEXT(": sublevel package dirty state unchanged"),Sub.Level->GetOutermost()->IsDirty()==bSubDirty);
    }
    {
        // 範囲の外にある非表示Levelのinstanceは影響を受けないので、拒否の理由にならない。
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
        FScopedSubLevel Sub(F.World);if(!TestTrue(TEXT("sublevel"),Sub.Valid()))return false;
        Sub.Add(FVector(500,500,0));Sub.Level->bIsVisible=false;
        TestEqual(TEXT("hidden level outside the edit"),Execute(F.Rect(100,true)).Foliage.Verdict,FString(TEXT("NO_FOLIAGE")));
    }
    {
        // 読み込まれていないstreaming level: 中身を検査できないので判定不能。読み込みはしない。
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
        ULevelStreamingDynamic* Streaming=NewObject<ULevelStreamingDynamic>(F.World,NAME_None,RF_Transient);
        Streaming->SetWorldAssetByPackageName(TEXT("/Temp/LandscapeMCP_V05_NotLoaded"));Streaming->SetShouldBeLoaded(false);Streaming->SetShouldBeVisible(false);
        F.World->AddStreamingLevel(Streaming);
        const auto Dry=Execute(F.Rect(100,true));
        TestEqual(TEXT("unloaded level unknown"),Dry.Foliage.Verdict,FString(TEXT("UNKNOWN")));
        TestTrue(TEXT("unloaded level listed"),Dry.Foliage.Unknowns.ContainsByPredicate([](const FString& U){return U.StartsWith(TEXT("LEVEL_NOT_LOADED: "));}));
        TestTrue(TEXT("still not loaded"),Streaming->GetLoadedLevel()==nullptr);
        TestEqual(TEXT("unloaded level refused"),Execute(F.Rect()).ErrorCode,FString(TEXT("FOLIAGE_SAFETY_CONFLICT")));
        F.World->RemoveStreamingLevel(Streaming);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05ActorFoliage,"LandscapeMCP.V05.Foliage.ActorFoliage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05ActorFoliage::RunTest(const FString&)
{
    FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
    UFoliageType_Actor* Template=NewObject<UFoliageType_Actor>(GetTransientPackage());Template->ActorClass=AStaticMeshActor::StaticClass();
    FFoliageInfo* Info=nullptr;UFoliageType* Added=F.IFA->AddFoliageType(Template,&Info);
    if(!TestTrue(TEXT("actor foliage type"),Added&&Info))return false;
    FFoliageInstance I;I.Location=FVector(3000,3000,0);Info->AddInstance(Added,I,nullptr);
    TestEqual(TEXT("actor foliage impl"),int32(Info->Type),int32(EFoliageImplType::Actor));
    const auto Original=F.Read();
    const auto Dry=Execute(F.Rect(100,true));
    TestEqual(TEXT("actor foliage unknown"),Dry.Foliage.Verdict,FString(TEXT("UNKNOWN")));
    TestTrue(TEXT("reason"),Dry.Foliage.Samples.ContainsByPredicate([](const FLandscapeMCPFoliageImpactSample& S){return S.Reason==TEXT("NON_STATIC_MESH_FOLIAGE");}));
    const auto O=Execute(F.Rect());TestEqual(TEXT("refused"),O.ErrorCode,FString(TEXT("FOLIAGE_SAFETY_CONFLICT")));
    TestTrue(TEXT("terrain unchanged"),F.Read().Heights==Original.Heights);
    // 判定不能な形式でも、編集範囲の外なら影響はない。
    FFixture G;if(!TestTrue(TEXT("fixture 2"),G.Valid()))return false;
    // AddFoliageTypeはasset以外の型を最初のIFAへ取り込むため、2つ目のfixtureには新しい型を作る。
    UFoliageType_Actor* Template2=NewObject<UFoliageType_Actor>(GetTransientPackage());Template2->ActorClass=AStaticMeshActor::StaticClass();
    FFoliageInfo* GInfo=nullptr;UFoliageType* GAdded=G.IFA->AddFoliageType(Template2,&GInfo);
    if(GAdded&&GInfo){FFoliageInstance Far;Far.Location=FVector(500,500,0);GInfo->AddInstance(GAdded,Far,nullptr);}
    TestEqual(TEXT("actor foliage outside the edit"),Execute(G.Rect(100,true)).Foliage.Verdict,FString(TEXT("NO_FOLIAGE")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05PreWrite,"LandscapeMCP.V05.Foliage.PreWriteRecheck",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05PreWrite::RunTest(const FString&)
{
    for(EApplyFault Fault:{EApplyFault::FoliageStaleSnapMode,EApplyFault::FoliageStaleUnknown})
    {
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
        F.Add(FVector(500,500,0));const auto Original=F.Read();const auto Instances=F.Instances();
        const int32 Undo=UToolsetLibrary::GetActiveUndoCount();FString Stage;
        SetAutomationApplyFault(Fault);const auto O=Execute(F.Rect(),&Stage);SetAutomationApplyFault(EApplyFault::None);
        AddInfo(FString::Printf(TEXT("PREWRITE fault=%d code=%s stage=%s message=%s"),int32(Fault),*O.ErrorCode,*Stage,*O.Message));
        TestEqual(TEXT("refused"),O.ErrorCode,FString(TEXT("FOLIAGE_SAFETY_CONFLICT")));TestEqual(TEXT("before the write"),Stage,FString(TEXT("preWrite")));
        TestTrue(TEXT("stale reason"),O.Message.Contains(TEXT("FOLIAGE_STALE: ")));
        TestEqual(TEXT("gate itself passed"),O.Foliage.Verdict,FString(TEXT("NO_FOLIAGE")));
        TestEqual(TEXT("no transaction"),UToolsetLibrary::GetActiveUndoCount(),Undo);TestEqual(TEXT("no write"),O.ModifiedCount,0);
        TestTrue(TEXT("terrain unchanged"),F.Read().Heights==Original.Heights);TestTrue(TEXT("foliage unchanged"),SameTransforms(Instances,F.Instances()));
        TestFalse(TEXT("no latch"),MutationDisabled());
        // 同じ要求は、状態が戻れば通る。
        TestEqual(TEXT("applies once the state is stable"),Execute(F.Rect()).Status,FString(TEXT("APPLIED")));
    }
    // 比較対象の単体確認: instanceが同じでもsnap mode・UNKNOWN・走査件数が違えば別の状態。
    Foliage::FInventory A;A.SnappingMode=TEXT("DontSnap");A.bSnappingSafe=true;A.ScannedCount=3;
    auto B=A;TestTrue(TEXT("same"),Foliage::SameSafetyState(A,B));
    B.SnappingMode=TEXT("SnapEverywhere");B.bSnappingSafe=false;TestFalse(TEXT("snap mode differs"),Foliage::SameSafetyState(A,B));TestTrue(TEXT("instances alone still equal"),Foliage::SameInstances(A,B));
    B=A;B.Unknowns.Add(TEXT("X"));TestFalse(TEXT("unknown differs"),Foliage::SameSafetyState(A,B));
    B=A;B.ScannedCount=4;TestFalse(TEXT("scan count differs"),Foliage::SameSafetyState(A,B));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05PostFoliage,"LandscapeMCP.V05.Foliage.PostFoliageAndRestore",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05PostFoliage::RunTest(const FString&)
{
    {
        // Transactionの中でinstanceが本当に動いた場合: postFoliageで失敗し、Undoで地形もFoliageも戻ってVERIFIED。
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
        F.Add(FVector(500,500,0));const auto Original=F.Read();const auto Instances=F.Instances();const uint32 IFAHash=Hash(F.IFA);
        FString Stage;SetAutomationApplyFault(EApplyFault::FoliageChangeInTransaction);const auto O=Execute(F.Rect(),&Stage);SetAutomationApplyFault(EApplyFault::None);
        AddInfo(FString::Printf(TEXT("POST_FOLIAGE in-transaction status=%s rollback=%s stage=%s message=%s"),*O.Status,*O.RollbackStatus,*Stage,*O.Message));
        TestEqual(TEXT("code"),O.ErrorCode,FString(TEXT("FOLIAGE_SAFETY_CONFLICT")));TestEqual(TEXT("stage"),Stage,FString(TEXT("postFoliage")));
        TestTrue(TEXT("post verify reason"),O.Message.Contains(TEXT("FOLIAGE_POST_VERIFY: foliage instances changed")));
        TestFalse(TEXT("instances changed detected"),O.Foliage.PostApply.bInstancesUnchanged);
        TestEqual(TEXT("rolled back"),O.Status,FString(TEXT("ROLLED_BACK")));TestEqual(TEXT("verified"),O.RollbackStatus,FString(TEXT("VERIFIED")));
        TestTrue(TEXT("terrain restored"),F.Read().Heights==Original.Heights);TestTrue(TEXT("foliage restored by Undo"),SameTransforms(Instances,F.Instances()));
        TestFalse(TEXT("no latch"),MutationDisabled());AddInfo(FString::Printf(TEXT("IFA hash before=%u after=%u"),IFAHash,Hash(F.IFA)));
    }
    {
        // Transactionの外でinstanceが動いた場合（遅延snapと同じ形）: Undoでは戻せないのでROLLBACK_FAILEDとlatch。
        // 製品の経路では、この状態を書き込み前の判定（DontSnap以外・付属・UNKNOWNの拒否）で防ぐ。
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
        F.Add(FVector(500,500,0));const auto Original=F.Read();const auto Instances=F.Instances();
        FString Stage;SetAutomationApplyFault(EApplyFault::FoliageChangeOutsideTransaction);const auto O=Execute(F.Rect(),&Stage);SetAutomationApplyFault(EApplyFault::None);
        AddInfo(FString::Printf(TEXT("POST_FOLIAGE outside-transaction status=%s rollback=%s stage=%s message=%s"),*O.Status,*O.RollbackStatus,*Stage,*O.Message));
        TestEqual(TEXT("code"),O.ErrorCode,FString(TEXT("FOLIAGE_SAFETY_CONFLICT")));TestEqual(TEXT("stage"),Stage,FString(TEXT("postFoliage")));
        TestEqual(TEXT("rollback failed"),O.Status,FString(TEXT("ROLLBACK_FAILED")));TestEqual(TEXT("failed"),O.RollbackStatus,FString(TEXT("FAILED")));
        TestTrue(TEXT("latch"),MutationDisabled());TestTrue(TEXT("restore reason"),O.Message.Contains(TEXT("Foliage instances differ")));
        TestTrue(TEXT("terrain itself restored"),F.Read().Heights==Original.Heights);TestFalse(TEXT("foliage really differs"),SameTransforms(Instances,F.Instances()));
        // latch中はApplyしない。Dry-runは使える。
        TestEqual(TEXT("latched apply refused"),Execute(F.Rect(50)).ErrorCode,FString(TEXT("MUTATION_DISABLED_AFTER_ROLLBACK_FAILURE")));
        TestTrue(TEXT("dry run after latch"),Execute(F.Rect(50,true)).bSuccess);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05InRange,"LandscapeMCP.V05.Foliage.InRangeApplyVerified",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05InRange::RunTest(const FString&)
{
    // 読み取り範囲の中で、4頂点とも変わらないセル(38,30)にinstanceを置く（falloffの外端x=3800は重み0）。
    FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
    const FVector2D XY(3850,3050);F.Add(FVector(XY.X,XY.Y,0));const auto Instances=F.Instances();
    const auto Dry=Execute(F.Rect(100,true));
    TestEqual(TEXT("inspected in range"),Dry.Foliage.InspectedCount,1);TestEqual(TEXT("safe"),Dry.Foliage.Verdict,FString(TEXT("SAFE_TO_APPLY")));
    const auto O=Execute(F.Rect());
    if(!TestEqual(*O.Message,O.Status,FString(TEXT("APPLIED"))))return false;
    TestEqual(TEXT("evaluation"),O.Foliage.Evaluation,FString(TEXT("APPLY")));
    TestTrue(TEXT("post checked"),O.Foliage.PostApply.bChecked);TestEqual(TEXT("verified one"),O.Foliage.PostApply.VerifiedCount,1);
    TestTrue(TEXT("prediction matched"),O.Foliage.PostApply.bTerrainMatchesPrediction);TestEqual(TEXT("prediction error"),O.Foliage.PostApply.MaxPredictionErrorCm,0.0);
    // 独立の照合: 書き込み後の実Collisionと、instanceのXYでの三角形高度。
    const auto Actual=F.Read();double Z=0,Slope=0,Collision=0;
    if(TestTrue(TEXT("terrain"),Foliage::TerrainAt(Actual,Actual.Heights,XY,Z,Slope))&&TestTrue(TEXT("trace"),Trace(F.World,XY,Collision,F.IFA)))
        TestEqual(TEXT("collision under the instance"),Collision,Z,0.05);
    TestTrue(TEXT("foliage unchanged"),SameTransforms(Instances,F.Instances()));
    // N1: 同じ要求の再実行は書き込みがなく、Foliageは今回あらためて評価したもの（過去の事後検証を流用しない）。
    const auto Retry=Execute(F.Rect());
    TestEqual(TEXT("retry no change"),Retry.Status,FString(TEXT("NO_CHANGE")));
    TestEqual(TEXT("retry evaluation is current"),Retry.Foliage.Evaluation,FString(TEXT("RETRY_CURRENT_STATE")));
    TestFalse(TEXT("retry has no post-apply verification"),Retry.Foliage.PostApply.bChecked);
    TestEqual(TEXT("retry current verdict"),Retry.Foliage.Verdict,FString(TEXT("SAFE_TO_APPLY")));
    // 再実行の前にsnap modeが変わっていれば、記録ではなく今の状態（BLOCKED）が報告される。
    {
        FScopedSnapMode Scope(*this,EFoliageLandscapeSnappingMode::SnapEverywhere);
        const auto Again=Execute(F.Rect());TestEqual(TEXT("retry still writes nothing"),Again.Status,FString(TEXT("NO_CHANGE")));
        TestEqual(TEXT("retry reports the current snap mode"),Again.Foliage.SnappingMode,FString(TEXT("SnapEverywhere")));
        TestEqual(TEXT("retry reports current risk"),Again.Foliage.Verdict,FString(TEXT("BLOCKED")));
    }
    return true;
}

namespace LandscapeMCP::FoliageTests
{
    // 実際のEditor tickを挟む（同期的に更新関数を繰り返すだけでは代替しない）。
    struct FMultiFrameState
    {
        TUniquePtr<FFixture> Fixture;TArray<FFoliageInstance> Instances;uint32 IFAHash=0;FHeightfieldSnapshot Applied,Original;
        uint64 StartFrame=0;double StartTime=0;int32 Step=0;
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05MultiFrame,"LandscapeMCP.V05.Foliage.MultiFrameStable",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05MultiFrame::RunTest(const FString&)
{
    TSharedRef<FMultiFrameState> State=MakeShared<FMultiFrameState>();
    State->Fixture=MakeUnique<FFixture>();FFixture& F=*State->Fixture;
    if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
    F.Add(FVector(500,500,0));F.Add(FVector(3850,3050,0));F.Add(FVector(6000,6000,0),false);
    State->Original=F.Read();
    const auto O=Execute(F.Rect());if(!TestEqual(*O.Message,O.Status,FString(TEXT("APPLIED"))))return false;
    State->Instances=F.Instances();State->IFAHash=Hash(F.IFA);State->Applied=F.Read();
    State->StartFrame=GFrameCounter;State->StartTime=FPlatformTime::Seconds();
    constexpr uint64 Frames=30;constexpr double Seconds=0.5;
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,State]()
    {
        FFixture& F=*State->Fixture;
        if(GFrameCounter<State->StartFrame+Frames||FPlatformTime::Seconds()<State->StartTime+Seconds)return false;
        if(State->Step==0)
        {
            AddInfo(FString::Printf(TEXT("MULTI_FRAME after apply frames=%llu seconds=%.3f"),GFrameCounter-State->StartFrame,FPlatformTime::Seconds()-State->StartTime));
            TestTrue(TEXT("frames really advanced"),GFrameCounter>=State->StartFrame+Frames);
            TestTrue(TEXT("foliage unchanged after frames"),SameTransforms(State->Instances,F.Instances()));
            TestEqual(TEXT("IFA bytes unchanged after frames"),Hash(F.IFA),State->IFAHash);
            TestTrue(TEXT("terrain stays applied after frames"),F.Read().Heights==State->Applied.Heights);
            TestTrue(TEXT("undo"),GEditor->UndoTransaction());
            State->Step=1;State->StartFrame=GFrameCounter;State->StartTime=FPlatformTime::Seconds();return false;
        }
        AddInfo(FString::Printf(TEXT("MULTI_FRAME after undo frames=%llu"),GFrameCounter-State->StartFrame));
        TestTrue(TEXT("terrain restored after undo and frames"),F.Read().Heights==State->Original.Heights);
        TestTrue(TEXT("foliage unchanged after undo and frames"),SameTransforms(State->Instances,F.Instances()));
        TestEqual(TEXT("no saves"),F.Saves,0);
        State->Fixture.Reset();return true;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05ScanExact,"LandscapeMCP.V05.Foliage.ScanLimitExact",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05ScanExact::RunTest(const FString&)
{
    FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
    // すべて編集のcore（2500..3500 x 2600..3400）の中。平坦な地面を目標0で平らにするので地形は変わらない。
    TArray<FFoliageInstance> Batch;Batch.SetNum(Foliage::MaxScannedInstances);
    TArray<const FFoliageInstance*> Pointers;
    for(int32 I=0;I<Batch.Num();++I){Batch[I].Location=FVector(2600+(I%256)*3.0,2700+(I/256)*2.0,0);Pointers.Add(&Batch[I]);}
    F.Info->AddInstances(F.Type,Pointers);
    auto Dry=Execute(F.Rect(0,true));
    TestFalse(TEXT("exactly the limit: complete"),Dry.Foliage.bScanLimitExceeded);TestEqual(TEXT("exactly the limit: counted"),Dry.Foliage.InspectedCount,Foliage::MaxScannedInstances);
    TestEqual(TEXT("exactly the limit: safe"),Dry.Foliage.Verdict,FString(TEXT("SAFE_TO_APPLY")));
    auto Inspect=FoliageInspect(F.Path,2500,2500,3500,3500,0);
    TestEqual(TEXT("inspect exact count"),Inspect.InstanceCount,Foliage::MaxScannedInstances);TestTrue(TEXT("inspect exact"),Inspect.bInstanceCountExact);
    TestTrue(TEXT("inspect max 0 truncates"),Inspect.bTruncated&&Inspect.Instances.IsEmpty());
    F.Add(FVector(3000,3000,0),false);
    Dry=Execute(F.Rect(0,true));
    TestTrue(TEXT("one over: exceeded"),Dry.Foliage.bScanLimitExceeded);TestEqual(TEXT("one over: count unknown"),Dry.Foliage.InspectedCount,-1);
    TestEqual(TEXT("one over: unknown"),Dry.Foliage.Verdict,FString(TEXT("UNKNOWN")));
    TestEqual(TEXT("one over: apply refused"),Execute(F.Rect(100)).ErrorCode,FString(TEXT("FOLIAGE_SAFETY_CONFLICT")));
    Inspect=FoliageInspect(F.Path,2500,2500,3500,3500,256);
    TestTrue(TEXT("inspect success"),Inspect.bSuccess);TestTrue(TEXT("inspect exceeded"),Inspect.bScanLimitExceeded);
    TestFalse(TEXT("inspect count not exact"),Inspect.bInstanceCountExact);TestEqual(TEXT("inspect count unknown"),Inspect.InstanceCount,-1);
    TestEqual(TEXT("scanned is a lower bound"),Inspect.ScannedInstanceCount,Foliage::MaxScannedInstances+1);
    TestEqual(TEXT("inspect status"),Inspect.Status,FString(TEXT("UNKNOWN_PRESENT")));TestTrue(TEXT("no partial list"),Inspect.Instances.IsEmpty()&&Inspect.Types.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05InspectLimits,"LandscapeMCP.V05.Foliage.InspectLimits",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05InspectLimits::RunTest(const FString&)
{
    FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
    for(int32 I=0;I<4100;++I)F.Add(FVector(2000+(I%64)*20.0,2000+(I/64)*20.0,0),false);
    auto R=FoliageInspect(F.Path,0,0,6300,6300,0);
    TestTrue(TEXT("0 accepted"),R.bSuccess);TestEqual(TEXT("0 returns none"),R.Instances.Num(),0);TestTrue(TEXT("0 truncated"),R.bTruncated);TestEqual(TEXT("0 count"),R.InstanceCount,4100);
    R=FoliageInspect(F.Path,0,0,6300,6300,4096);
    TestTrue(TEXT("4096 accepted"),R.bSuccess);TestEqual(TEXT("4096 returned"),R.Instances.Num(),4096);TestTrue(TEXT("4096 truncated"),R.bTruncated);
    TestFalse(TEXT("4097 rejected"),FoliageInspect(F.Path,0,0,6300,6300,4097).bSuccess);
    TestFalse(TEXT("INT_MAX rejected"),FoliageInspect(F.Path,0,0,6300,6300,MAX_int32).bSuccess);
    TestFalse(TEXT("+Infinity rejected"),FoliageInspect(F.Path,0,0,INFINITY,6300,16).bSuccess);
    TestFalse(TEXT("-Infinity rejected"),FoliageInspect(F.Path,-INFINITY,0,100,100,16).bSuccess);
    TestFalse(TEXT("NaN rejected"),FoliageInspect(F.Path,0,NAN,100,100,16).bSuccess);
    TestFalse(TEXT("huge coordinate rejected"),FoliageInspect(F.Path,0,0,1.e12,100,16).bSuccess);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05SchemaV040,"LandscapeMCP.V05.Schema.V040Compatible",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05SchemaV040::RunTest(const FString&)
{
    // v0.4.0 tag（898e570）のschema。既存13 Toolは、3 Toolの出力へ`foliage`を足したこと以外すべて同一であること。
    const FString Schema=UToolsetRegistry::GetToolsetJsonSchema(ULandscapeMCPToolset::StaticClass());
    TSharedPtr<FJsonObject> Current,Baseline;FString Text;
    const FString File=FPaths::ProjectPluginsDir()/TEXT("LandscapeMCP/Source/LandscapeMCP/Private/Tests/Fixtures/LandscapeMCP_v040_schema.json");
    if(!TestTrue(TEXT("baseline"),FFileHelper::LoadFileToString(Text,*File)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Baseline)&&Baseline.IsValid()))return false;
    if(!TestTrue(TEXT("current"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Schema),Current)&&Current.IsValid()))return false;
    TestEqual(TEXT("baseline has 13 tools"),Baseline->GetArrayField(TEXT("tools")).Num(),13);
    int32 Compared=0,WithFoliage=0;
    for(const auto& Old:Baseline->GetArrayField(TEXT("tools")))
    {
        const FString Name=Old->AsObject()->GetStringField(TEXT("name"));
        const TSharedPtr<FJsonValue>* Found=Current->GetArrayField(TEXT("tools")).FindByPredicate([&](const TSharedPtr<FJsonValue>& V){return V->AsObject()->GetStringField(TEXT("name"))==Name;});
        if(!TestNotNull(*Name,Found))continue;
        // 比較用の複製から、追加した`foliage`だけを取り除く。
        TSharedPtr<FJsonValue> Copy;FString CurrentText;auto W=TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&CurrentText);
        FJsonSerializer::Serialize((*Found)->AsObject().ToSharedRef(),W);FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(CurrentText),Copy);
        const TSharedPtr<FJsonObject>* Output=nullptr;
        if(Copy->AsObject()->TryGetObjectField(TEXT("outputSchema"),Output))
        {
            const TSharedPtr<FJsonObject> Return=(*Output)->GetObjectField(TEXT("properties"))->GetObjectField(TEXT("returnValue"));
            const TSharedPtr<FJsonObject> Props=Return->GetObjectField(TEXT("properties"));
            if(Props->HasField(TEXT("foliage")))
            {
                ++WithFoliage;Props->RemoveField(TEXT("foliage"));
                TArray<TSharedPtr<FJsonValue>> Required;
                if(Return->HasTypedField<EJson::Array>(TEXT("required")))
                {
                    for(const auto& R:Return->GetArrayField(TEXT("required")))if(R->AsString()!=TEXT("foliage"))Required.Add(R);
                    Return->SetArrayField(TEXT("required"),Required);
                }
            }
        }
        ++Compared;TestTrue(*(Name+TEXT(" equals v0.4.0 except the additive foliage output")),FJsonValue::CompareEqual(*Old,*Copy));
    }
    TestEqual(TEXT("all 13 compared"),Compared,13);
    TestEqual(TEXT("foliage added only to the 3 terrain operation tools"),WithFoliage,3);
    return true;
}

namespace LandscapeMCP::FoliageTests
{
    FString ToolsetName()
    {
        // スキーマのTool名 "<toolset>.<tool>" からtoolset名を取る。
        TSharedPtr<FJsonObject> Schema;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(UToolsetRegistry::GetToolsetJsonSchema(ULandscapeMCPToolset::StaticClass())),Schema);
        FString Name=Schema.IsValid()?Schema->GetArrayField(TEXT("tools"))[0]->AsObject()->GetStringField(TEXT("name")):FString();
        int32 Dot=INDEX_NONE;Name.FindLastChar(TEXT('.'),Dot);return Dot==INDEX_NONE?Name:Name.Left(Dot);
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05ToolCall,"LandscapeMCP.V05.MCP.ToolCallPaths",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05ToolCall::RunTest(const FString&)
{
    // AIが呼ぶのと同じ経路（ToolsetRegistry.ExecuteTool）。PythonのBlueprint公開APIも同じ関数を呼ぶ。
    FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
    F.Add(FVector(3000,3000,0));
    const FString Toolset=ToolsetName();AddInfo(TEXT("TOOLSET ")+Toolset);
    auto Call=[&](const TCHAR* Tool,const FString& Json,FString& Value,FString& Error)
    {
        UToolCallAsyncResultString* R=UToolsetRegistry::ExecuteTool(Toolset,Tool,Json);
        if(!R||!R->bIsComplete){Error=TEXT("not complete");return false;}
        Value=R->Value;Error=R->Error;return Error.IsEmpty();
    };
    AddExpectedMessage(TEXT("LandscapeMCP: "),ELogVerbosity::Warning,EAutomationExpectedMessageFlags::Contains,0,false);
    FString Value,Error;
    TestTrue(*(TEXT("GetCapabilities: ")+Error),Call(TEXT("GetCapabilities"),TEXT("{}"),Value,Error));
    TestTrue(TEXT("capabilities JSON"),Value.Contains(TEXT("\"engine\""))&&Value.Contains(TEXT("UnrealEngine"))&&Value.Contains(TEXT("PRESERVE_IN_PLACE_ONLY")));
    AddInfo(TEXT("TOOLCALL GetCapabilities ")+Value.Left(600));
    const FString Ok=FString::Printf(TEXT("{\"landscapePath\":\"%s\",\"minX\":2500,\"minY\":2500,\"maxX\":3500,\"maxY\":3500,\"maxInstances\":16}"),*F.Path);
    TestTrue(*(TEXT("InspectFoliage: ")+Error),Call(TEXT("InspectFoliage"),Ok,Value,Error));
    TestTrue(TEXT("inspect JSON"),Value.Contains(TEXT("\"instanceCount\":1"))||Value.Contains(TEXT("\"instanceCount\": 1")));
    AddInfo(TEXT("TOOLCALL InspectFoliage ")+Value.Left(600));
    const FString Bad=FString::Printf(TEXT("{\"landscapePath\":\"%s\",\"minX\":2500,\"minY\":2500,\"maxX\":3500,\"maxY\":3500,\"maxInstances\":5000}"),*F.Path);
    TestFalse(TEXT("InspectFoliage over the limit is an error"),Call(TEXT("InspectFoliage"),Bad,Value,Error));
    AddInfo(TEXT("TOOLCALL InspectFoliage error ")+Error.Left(600));
    TestTrue(TEXT("error names the limit"),Error.Contains(TEXT("MaxInstances")));
    const FString Reversed=FString::Printf(TEXT("{\"landscapePath\":\"%s\",\"minX\":3500,\"minY\":3500,\"maxX\":2500,\"maxY\":2500}"),*F.Path);
    TestFalse(TEXT("reversed bounds error"),Call(TEXT("InspectFoliage"),Reversed,Value,Error));
    TestFalse(TEXT("missing required argument"),Call(TEXT("InspectFoliage"),TEXT("{}"),Value,Error));AddInfo(TEXT("TOOLCALL missing ")+Error.Left(300));
    // Python（Editor Scripting）。PythonScriptPluginがあるときだけ実行し、結果ファイルで確認する。
    const FString Out=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("LandscapeMCP_V05_Python.json"));
    const FString Script=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("LandscapeMCP_V05_Python.py"));
    IFileManager::Get().Delete(*Out);
    const FString Py=FString::Printf(TEXT(
        "import json, unreal\n"
        "out = {}\n"
        "def call(tool, args):\n"
        "    r = unreal.ToolsetRegistry.execute_tool('%s', tool, json.dumps(args))\n"
        "    return {'complete': r.is_complete, 'error': r.error, 'value': r.value}\n"
        "out['capabilities'] = call('GetCapabilities', {})\n"
        "out['inspect'] = call('InspectFoliage', {'landscapePath': '%s', 'minX': 2500, 'minY': 2500, 'maxX': 3500, 'maxY': 3500, 'maxInstances': 16})\n"
        "out['inspectError'] = call('InspectFoliage', {'landscapePath': '%s', 'minX': 2500, 'minY': 2500, 'maxX': 3500, 'maxY': 3500, 'maxInstances': 5000})\n"
        "open(r'%s', 'w', encoding='utf-8').write(json.dumps(out))\n"),*Toolset,*F.Path,*F.Path,*Out);
    FFileHelper::SaveStringToFile(Py,*Script,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if(!FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin")))
    {AddWarning(TEXT("PYTHON_NOT_RUN: PythonScriptPlugin is not loaded in this project; the Python path was not exercised."));return true;}
    GEditor->Exec(F.World,*FString::Printf(TEXT("py \"%s\""),*Script));
    FString Result;
    if(!TestTrue(TEXT("python wrote the result"),FFileHelper::LoadFileToString(Result,*Out)))return true;
    AddInfo(TEXT("PYTHON ")+Result.Left(1500));
    TSharedPtr<FJsonObject> P;if(!TestTrue(TEXT("python JSON"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Result),P)&&P.IsValid()))return true;
    TestTrue(TEXT("python capabilities"),P->GetObjectField(TEXT("capabilities"))->GetBoolField(TEXT("complete"))&&P->GetObjectField(TEXT("capabilities"))->GetStringField(TEXT("error")).IsEmpty());
    TestTrue(TEXT("python inspect"),P->GetObjectField(TEXT("inspect"))->GetStringField(TEXT("error")).IsEmpty()&&P->GetObjectField(TEXT("inspect"))->GetStringField(TEXT("value")).Contains(TEXT("instanceCount")));
    TestFalse(TEXT("python inspect error reported"),P->GetObjectField(TEXT("inspectError"))->GetStringField(TEXT("error")).IsEmpty());
    IFileManager::Get().Delete(*Out);IFileManager::Get().Delete(*Script);
    return true;
}

namespace LandscapeMCP::FoliageTests
{
    // 外部HTTP client（harnessのmcp_client_v05.py）と、新Tool 2つとFoliage付きFlattenRectangleを往復する。
    class FV05TransportCommand final:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;FString Directory;TUniquePtr<FFixture> Fixture;FHeightfieldSnapshot Original;TArray<FFoliageInstance> Instances;
        uint32 IFAHash=0;int32 UndoBefore=0;double Deadline=0;
    public:
        FV05TransportCommand(FAutomationTestBase* T,FString D):Test(T),Directory(MoveTemp(D)){}
        virtual bool Update()override
        {
            if(!Fixture)
            {
                Fixture=MakeUnique<FFixture>();if(!Fixture->Valid()){Test->AddError(TEXT("V05 HTTP fixture creation failed."));return true;}
                Fixture->Add(FVector(3000,3000,0));Fixture->Add(FVector(500,500,0));
                Original=Fixture->Read();Instances=Fixture->Instances();IFAHash=Hash(Fixture->IFA);UndoBefore=UToolsetLibrary::GetActiveUndoCount();
                IFileManager::Get().MakeDirectory(*Directory,true);
                FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"landscapePath\":\"%s\"}"),*Fixture->Path),*(Directory/TEXT("ready0.json")));
                Deadline=FPlatformTime::Seconds()+180;return false;
            }
            if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("V05 HTTP harness timed out waiting for the external client."));Fixture.Reset();return true;}
            if(!IFileManager::Get().FileExists(*(Directory/TEXT("done0"))))return false;
            Test->TestEqual(TEXT("HTTP: no transaction"),UToolsetLibrary::GetActiveUndoCount(),UndoBefore);
            Test->TestTrue(TEXT("HTTP: terrain unchanged"),Fixture->Read().Heights==Original.Heights);
            Test->TestTrue(TEXT("HTTP: foliage unchanged"),SameTransforms(Instances,Fixture->Instances()));
            Test->TestEqual(TEXT("HTTP: IFA bytes unchanged"),Hash(Fixture->IFA),IFAHash);Test->TestEqual(TEXT("HTTP: no saves"),Fixture->Saves,0);
            Test->TestFalse(TEXT("HTTP: no latch"),MutationDisabled());
            Fixture.Reset();FFileHelper::SaveStringToFile(TEXT("complete"),*(Directory/TEXT("complete")));return true;
        }
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05Transport,"LandscapeMCP.V05.MCP.HttpRoundTrip",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05Transport::RunTest(const FString&)
{
    FString Directory;
    if(!FParse::Value(FCommandLine::Get(),TEXT("LandscapeMCPTransportHarness="),Directory))
    {AddWarning(TEXT("HTTP_NOT_RUN: run with -LandscapeMCPTransportHarness=<directory> and harness mcp_client_v05.py; this run does not prove the V05 HTTP path."));return true;}
    AddExpectedMessage(TEXT("LandscapeMCP: "),ELogVerbosity::Warning,EAutomationExpectedMessageFlags::Contains,0,false);
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<LandscapeMCP::FoliageTests::FV05TransportCommand>(this,Directory/TEXT("V05")));return true;
}

// ---------------------------------------------------------------------------------------------
// v0.4から既存の対象解決の確認（同じLandscapeの別proxy、Heightmap共有）が、非表示Levelを見ていること。
// Editorで非表示にしたstreaming levelは、World->Levelsに残りbIsVisible=falseになる（UWorld::RemoveFromWorldはgame worldだけ外す）。
// ---------------------------------------------------------------------------------------------
namespace LandscapeMCP::FoliageTests
{
    ALandscapeStreamingProxy* SpawnProxy(FScopedSubLevel& Sub,const FGuid& Guid)
    {
        FActorSpawnParameters P;P.OverrideLevel=Sub.Level;P.bDeferConstruction=true;
        ALandscapeStreamingProxy* Proxy=Sub.Sub->SpawnActor<ALandscapeStreamingProxy>(ALandscapeStreamingProxy::StaticClass(),FTransform::Identity,P);
        if(!Proxy)return nullptr;Proxy->SetLandscapeGuid(Guid,false);Proxy->FinishSpawning(FTransform::Identity);return Proxy;
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05HiddenProxy,"LandscapeMCP.V05.Resolve.HiddenLevelProxies",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05HiddenProxy::RunTest(const FString&)
{
    struct FMode{const TCHAR* Name;bool bVisible;bool bLocked;};
    const FMode Modes[]={{TEXT("visible"),true,false},{TEXT("hidden"),false,false},{TEXT("hidden+locked"),false,true}};
    // (1) 同じLandscape GUIDの別proxyがsublevelにある: 表示・非表示に関わらず、読み取りも書き込みも拒否（v0.4の契約）。
    for(const FMode& M:Modes)
    {
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Actor!=nullptr))return false;
        FScopedSubLevel Sub(F.World);if(!TestTrue(TEXT("sublevel"),Sub.Level!=nullptr))return false;
        ALandscapeStreamingProxy* Proxy=SpawnProxy(Sub,F.Actor->GetLandscapeGuid());if(!TestNotNull(TEXT("proxy"),Proxy))return false;
        Sub.Level->bIsVisible=M.bVisible;
#if WITH_EDITORONLY_DATA
        Sub.Level->bLocked=M.bLocked;
#endif
        bool bDefault=false;for(TActorIterator<ALandscapeProxy> It(F.World);It;++It)bDefault|=*It==Proxy;
        TestTrue(FString(M.Name)+TEXT(": default iterator sees only visible levels"),bDefault==M.bVisible);
        const auto H=Height(F.Path,3000,3000);
        TestFalse(FString(M.Name)+TEXT(": GetHeight refused"),H.bSuccess);TestTrue(FString(M.Name)+TEXT(": reason ")+H.Message,H.Message.Contains(TEXT("another proxy")));
        const auto Dry=Execute(F.Rect(100,true));TestFalse(FString(M.Name)+TEXT(": dry run refused"),Dry.bSuccess);
        const auto Apply=Execute(F.Rect(100));TestFalse(FString(M.Name)+TEXT(": apply refused"),Apply.bSuccess);TestEqual(FString(M.Name)+TEXT(": nothing written"),Apply.ModifiedCount,0);
        TestFalse(FString(M.Name)+TEXT(": inspect refused"),FoliageInspect(F.Path,2500,2500,3500,3500,16).bSuccess);
        TestTrue(FString(M.Name)+TEXT(": visibility unchanged"),bool(Sub.Level->bIsVisible)==M.bVisible);
    }
    // (2) 別のLandscape（別GUID）がsublevelにあり、対象のHeightmap textureを共有している: 非表示でも拒否。
    for(const FMode& M:Modes)
    {
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Actor!=nullptr))return false;
        FScopedSubLevel Sub(F.World);if(!TestTrue(TEXT("sublevel"),Sub.Level!=nullptr))return false;
        ALandscapeStreamingProxy* Proxy=SpawnProxy(Sub,FGuid::NewGuid());if(!TestNotNull(TEXT("proxy"),Proxy))return false;
        ULandscapeComponent* Shared=NewObject<ULandscapeComponent>(Proxy);Shared->SetHeightmap(F.Actor->LandscapeComponents[0]->GetHeightmap());Proxy->LandscapeComponents.Add(Shared);
        Sub.Level->bIsVisible=M.bVisible;
#if WITH_EDITORONLY_DATA
        Sub.Level->bLocked=M.bLocked;
#endif
        const auto H=Height(F.Path,3000,3000);
        TestFalse(FString(M.Name)+TEXT(": shared heightmap refused"),H.bSuccess);TestTrue(FString(M.Name)+TEXT(": shared reason ")+H.Message,H.Message.Contains(TEXT("shared by another Landscape")));
        const auto Apply=Execute(F.Rect(100));TestFalse(FString(M.Name)+TEXT(": shared apply refused"),Apply.bSuccess);TestEqual(FString(M.Name)+TEXT(": shared nothing written"),Apply.ModifiedCount,0);
        Proxy->LandscapeComponents.Reset();
    }
    // (3) 読み込まれていないstreaming level: 中に同じLandscapeのproxyがあるか分からない。
    //     読み取りは対象自身のcomponentだけを読むので許可。書き込み（円形ブラシ、v0.4の3 Tool）は拒否。
    {
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Actor!=nullptr))return false;
        ULevelStreamingDynamic* Streaming=NewObject<ULevelStreamingDynamic>(F.World,NAME_None,RF_Transient);
        Streaming->SetWorldAssetByPackageName(TEXT("/Temp/LandscapeMCP_V05_NotLoadedProxy"));Streaming->SetShouldBeLoaded(false);Streaming->SetShouldBeVisible(false);
        F.World->AddStreamingLevel(Streaming);
        const auto Original=F.Read();
        TestTrue(TEXT("unloaded: GetHeight allowed"),Height(F.Path,3000,3000).bSuccess);
        TestTrue(TEXT("unloaded: region read allowed"),HeightRegion(F.Path,2000,2000,4000,4000,100).bSuccess);
        TestTrue(TEXT("unloaded: dry run allowed"),Execute(F.Rect(100,true)).bSuccess);
        FEditRequest E;E.LandscapePath=F.Path;E.Center=FVector2D(3000,3000);E.RadiusCm=300;E.Strength=100;E.bDryRun=false;
        const auto Brush=Edit(E);TestFalse(TEXT("unloaded: circular brush write refused"),Brush.bSuccess);
        TestTrue(TEXT("unloaded: brush reason ")+Brush.Message,Brush.Message.Contains(TEXT("not loaded")));
        const auto Apply=Execute(F.Rect(100));TestFalse(TEXT("unloaded: apply refused"),Apply.bSuccess);TestEqual(TEXT("unloaded: apply code"),Apply.ErrorCode,FString(TEXT("FOLIAGE_SAFETY_CONFLICT")));
        TestTrue(TEXT("unloaded: terrain unchanged"),F.Read().Heights==Original.Heights);TestTrue(TEXT("unloaded: still not loaded"),Streaming->GetLoadedLevel()==nullptr);
        F.World->RemoveStreamingLevel(Streaming);
        // streaming levelがなければ、同じ書き込みは通る（拒否の理由がunloadedだけであること）。
        TestTrue(TEXT("without the unloaded level the brush applies"),Edit(E).bSuccess);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// 円形ブラシ3 Tool（Sculpt / Smooth / FlattenRegion）は、付属Foliageがあれば書き込まない（v0.4の契約）。
// 付属Foliageが非表示・ロック中のLevelのIFAにある場合も同じであること。
// ---------------------------------------------------------------------------------------------
namespace LandscapeMCP::FoliageTests
{
    struct FFoliageState
    {
        int32 Count=0;TArray<FFoliageInstance> Instances;TArray<FFoliageInstanceBaseId> Bases;TArray<UActorComponent*> Components;
        bool operator==(const FFoliageState& B)const
        {
            if(Count!=B.Count||Bases!=B.Bases||Components!=B.Components||!SameTransforms(Instances,B.Instances))return false;
            return true;
        }
    };
    FFoliageState Capture(const FFoliageInfo* Info)
    {
        FFoliageState S;if(!Info)return S;S.Count=Info->Instances.Num();S.Instances=Info->Instances;
        for(const FFoliageInstance& I:Info->Instances){S.Bases.Add(I.BaseId);S.Components.Add(I.BaseComponent);}
        return S;
    }
    FEditRequest Brush(const FString& Path,EOperation Op,bool bDry)
    {
        FEditRequest E;E.LandscapePath=Path;E.Center=FVector2D(3000,3000);E.RadiusCm=400;E.Falloff=0.5;E.bDryRun=bDry;E.Operation=Op;
        E.Strength=Op==EOperation::Sculpt?50:0.5;E.TargetHeightCm=100;return E;
    }
    const TCHAR* OpName(EOperation Op){return Op==EOperation::Sculpt?TEXT("SculptRegion"):Op==EOperation::Smooth?TEXT("SmoothRegion"):TEXT("FlattenRegion");}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05BrushFoliage,"LandscapeMCP.V05.Resolve.BrushAttachedFoliageAllLevels",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05BrushFoliage::RunTest(const FString&)
{
    enum class ECase{SubVisible,SubHidden,SubHiddenLocked,NoAttached,SameLevel,Unloaded};
    struct FCase{const TCHAR* Name;ECase Kind;bool bRefused;};
    const FCase Cases[]={
        {TEXT("sublevel visible"),ECase::SubVisible,true},{TEXT("sublevel hidden"),ECase::SubHidden,true},
        {TEXT("sublevel hidden+locked"),ECase::SubHiddenLocked,true},{TEXT("no attached foliage"),ECase::NoAttached,false},
        {TEXT("same level attached"),ECase::SameLevel,true},{TEXT("unloaded streaming level"),ECase::Unloaded,true}};
    for(const EOperation Op:{EOperation::Sculpt,EOperation::Smooth,EOperation::Flatten})
    for(const FCase& C:Cases)
    {
        const FString Name=FString(OpName(Op))+TEXT(" / ")+C.Name;
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
        UActorComponent* Collision=F.Actor->LandscapeComponents[0]->GetCollisionComponent();
        TUniquePtr<FScopedSubLevel> Sub;ULevelStreamingDynamic* Streaming=nullptr;FFoliageInfo* Watched=F.Info;
        if(C.Kind==ECase::SubVisible||C.Kind==ECase::SubHidden||C.Kind==ECase::SubHiddenLocked||C.Kind==ECase::NoAttached)
        {
            Sub=MakeUnique<FScopedSubLevel>(F.World);if(!TestTrue(TEXT("sublevel"),Sub->Valid()))return false;
            // 対象Landscapeのcollision componentへ実際に付属させる（Base=nullptrでは代替しない）。
            FFoliageInstance I;I.Location=FVector(3000,3000,0);
            if(C.Kind==ECase::NoAttached){Sub->Info->AddInstance(Sub->Type,I,nullptr);}
            else{I.BaseComponent=Collision;Sub->Info->AddInstance(Sub->Type,I,Collision);}
            Watched=Sub->Info;
            if(C.Kind!=ECase::NoAttached)
            {
                const FFoliageInstanceBaseId Id=Sub->IFA->InstanceBaseCache.GetInstanceBaseId(Collision);
                TestTrue(Name+TEXT(": really attached (base id)"),Id!=FFoliageInstanceBaseCache::InvalidBaseId&&Sub->Info->Instances[0].BaseId==Id);
                TestTrue(Name+TEXT(": really attached (component hash)"),Sub->Info->ComponentHash.Contains(Id));
                TestTrue(Name+TEXT(": really attached (base component)"),Sub->Info->Instances[0].BaseComponent==Collision);
            }
            Sub->Level->bIsVisible=C.Kind!=ECase::SubHidden&&C.Kind!=ECase::SubHiddenLocked;
#if WITH_EDITORONLY_DATA
            Sub->Level->bLocked=C.Kind==ECase::SubHiddenLocked;
#endif
            if(C.Kind!=ECase::NoAttached)
                TestTrue(Name+TEXT(": engine HasFoliageAttached sees only visible levels"),AInstancedFoliageActor::HasFoliageAttached(Collision)==bool(Sub->Level->bIsVisible));
        }
        else if(C.Kind==ECase::SameLevel){F.Add(FVector(3000,3000,0));}
        else
        {
            Streaming=NewObject<ULevelStreamingDynamic>(F.World,NAME_None,RF_Transient);
            Streaming->SetWorldAssetByPackageName(TEXT("/Temp/LandscapeMCP_V05_NotLoadedBrush"));Streaming->SetShouldBeLoaded(false);Streaming->SetShouldBeVisible(false);
            F.World->AddStreamingLevel(Streaming);
        }
        UPackage* Package=F.World->GetOutermost();Package->SetDirtyFlag(false);
        UPackage* SubPackage=Sub?Sub->Level->GetOutermost():nullptr;if(SubPackage)SubPackage->SetDirtyFlag(false);
        const bool bVisible=Sub?bool(Sub->Level->bIsVisible):true;
#if WITH_EDITORONLY_DATA
        const bool bLocked=Sub?bool(Sub->Level->bLocked):false;
#endif
        const int32 Levels=F.World->GetLevels().Num();const auto Original=F.Read();const FFoliageState Before=Capture(Watched);
        const int32 Undo=UToolsetLibrary::GetActiveUndoCount();
        const auto Dry=Edit(Brush(F.Path,Op,true));const auto Apply=Edit(Brush(F.Path,Op,false));
        if(C.bRefused)
        {
            TestFalse(Name+TEXT(": dry run refused"),Dry.bSuccess);TestFalse(Name+TEXT(": apply refused"),Apply.bSuccess);
            TestTrue(Name+TEXT(": reason ")+Apply.Message,Apply.Message.Contains(C.Kind==ECase::Unloaded?TEXT("not loaded"):TEXT("Attached foliage is unsupported")));
            TestTrue(Name+TEXT(": heightmap unchanged"),F.Read().Heights==Original.Heights);
            TestTrue(Name+TEXT(": foliage count/transform/base unchanged"),Capture(Watched)==Before);
            TestEqual(Name+TEXT(": undo history unchanged"),UToolsetLibrary::GetActiveUndoCount(),Undo);
            TestFalse(Name+TEXT(": package not dirtied"),Package->IsDirty());
            if(SubPackage)TestFalse(Name+TEXT(": sublevel package not dirtied"),SubPackage->IsDirty());
        }
        else
        {
            TestTrue(Name+TEXT(": dry run ")+Dry.Message,Dry.bSuccess);TestTrue(Name+TEXT(": apply ")+Apply.Message,Apply.bSuccess);
            TestTrue(Name+TEXT(": unattached foliage unchanged"),Capture(Watched)==Before);
        }
        TestEqual(Name+TEXT(": level count unchanged"),F.World->GetLevels().Num(),Levels);
        if(Sub)
        {
            TestTrue(Name+TEXT(": visibility unchanged"),bool(Sub->Level->bIsVisible)==bVisible);
#if WITH_EDITORONLY_DATA
            TestTrue(Name+TEXT(": lock unchanged"),bool(Sub->Level->bLocked)==bLocked);
#endif
        }
        if(Streaming){TestTrue(Name+TEXT(": streaming level still not loaded"),Streaming->GetLoadedLevel()==nullptr);F.World->RemoveStreamingLevel(Streaming);}
        // 書き込みが成功した場合は、次のfixtureへ残さないよう自分のTransactionを戻す。
        if(Apply.bSuccess&&UToolsetLibrary::GetActiveUndoCount()>Undo)GEditor->UndoTransaction(false);
        Sub.Reset();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05BrushBaseIds,"LandscapeMCP.V05.Resolve.BrushStaleAndUnresolvedBase",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05BrushBaseIds::RunTest(const FString&)
{
    {
        // 使われなくなった（stale）base ID: 付属instanceを消してもbase cacheには残る。v0.4のHasFoliageAttachedと同じく拒否（緩めない）。
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
        FScopedSubLevel Sub(F.World);if(!TestTrue(TEXT("sublevel"),Sub.Valid()))return false;
        UActorComponent* Collision=F.Actor->LandscapeComponents[0]->GetCollisionComponent();
        FFoliageInstance I;I.Location=FVector(3000,3000,0);I.BaseComponent=Collision;Sub.Info->AddInstance(Sub.Type,I,Collision);
        const int32 Remove[1]={0};Sub.Info->RemoveInstances(Remove,true);
        const FFoliageInstanceBaseId Id=Sub.IFA->InstanceBaseCache.GetInstanceBaseId(Collision);
        TestEqual(TEXT("no instances left"),Sub.Info->Instances.Num(),0);
        AddInfo(FString::Printf(TEXT("STALE baseIdStillCached=%d componentHashEntry=%d"),Id!=FFoliageInstanceBaseCache::InvalidBaseId,Sub.Info->ComponentHash.Contains(Id)&&!Sub.Info->ComponentHash[Id].IsEmpty()));
        const bool bEngine=AInstancedFoliageActor::HasFoliageAttached(Collision);
        Sub.Level->bIsVisible=false;
        const auto R=Edit(Brush(F.Path,EOperation::Sculpt,false));
        TestTrue(TEXT("stale id: same decision as the engine check on a visible level"),R.bSuccess!=bEngine);
    }
    {
        // instanceが使うbase IDの対応がcacheに無い（付属先を証明できない）: 拒否。
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
        FScopedSubLevel Sub(F.World);if(!TestTrue(TEXT("sublevel"),Sub.Valid()))return false;
        Sub.Add(FVector(3000,3000,0));Sub.Level->bIsVisible=false;
        const FFoliageInstanceBaseId Dangling=123456;
        Sub.Info->ComponentHash.Add(Dangling,TSet<int32>({0}));   // テスト専用: transientなeditor dataだけを書き換える
        const auto Original=F.Read();
        const auto R=Edit(Brush(F.Path,EOperation::Sculpt,false));
        TestFalse(TEXT("unresolved base refused"),R.bSuccess);TestTrue(TEXT("unresolved reason ")+R.Message,R.Message.Contains(TEXT("cannot be ruled out")));
        TestTrue(TEXT("unresolved: heightmap unchanged"),F.Read().Heights==Original.Heights);
        TestTrue(TEXT("unresolved: inventory reports attachment risk"),FoliageInspect(F.Path,0,0,6300,6300,0).bTargetHasAttachedFoliage);
        Sub.Info->ComponentHash.Remove(Dangling);
        TestTrue(TEXT("without the dangling id the brush applies"),Edit(Brush(F.Path,EOperation::Sculpt,false)).bSuccess);
        if(GEditor->Trans->CanUndo())GEditor->UndoTransaction(false);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// 付属先の解決（Codex再レビューのMAJOR）: 使用中の有効BaseIdは、対象collisionを指す（Attached）か、
// 有効な別componentへ解決できる（None）場合だけ区別できる。entry欠損・null・解決不能・無効component・
// ComponentHashとinstanceの不整合は、付属先を証明できない（Unresolved）として書き込まない。
// ---------------------------------------------------------------------------------------------
namespace LandscapeMCP::FoliageTests
{
    struct FCacheState
    {
        TMap<FFoliageInstanceBaseId,FString> Map;int32 InvNum=0;FFoliageInstanceBaseId Next=0;
        bool operator==(const FCacheState& B)const{return Map.OrderIndependentCompareEqual(B.Map)&&InvNum==B.InvNum&&Next==B.Next;}
    };
    FCacheState CaptureCache(const AInstancedFoliageActor* IFA)
    {
        FCacheState S;const auto& C=IFA->InstanceBaseCache;
        for(const auto& Pair:C.InstanceBaseMap)S.Map.Add(Pair.Key,Pair.Value.BasePtr.ToSoftObjectPath().ToString());
        S.InvNum=C.InstanceBaseInvMap.Num();S.Next=C.NextBaseId;return S;
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05UnresolvedBase,"LandscapeMCP.V05.Resolve.UnresolvedBaseReference",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05UnresolvedBase::RunTest(const FString&)
{
    enum class EKind{EntryMissing,NullPtr,UnresolvablePath,DestroyedComponent,HashMismatch,MixedSafeFirst,OtherValid,TargetAttached,StaleTarget};
    struct FCase{const TCHAR* Name;EKind Kind;int32 Expected;};   // 0=None（書き込める） 1=Attached 2=Unresolved
    const FCase Cases[]={
        {TEXT("entry missing"),EKind::EntryMissing,2},{TEXT("BasePtr null"),EKind::NullPtr,2},
        {TEXT("unresolvable soft path"),EKind::UnresolvablePath,2},{TEXT("destroyed component"),EKind::DestroyedComponent,2},
        {TEXT("ComponentHash/BaseId mismatch"),EKind::HashMismatch,2},{TEXT("safe then unresolved (2 types)"),EKind::MixedSafeFirst,2},
        {TEXT("valid other component"),EKind::OtherValid,0},{TEXT("attached to target collision"),EKind::TargetAttached,1},
        {TEXT("stale target id"),EKind::StaleTarget,1}};
    for(const FCase& C:Cases)
    {
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
        UActorComponent* Collision=F.Actor->LandscapeComponents[0]->GetCollisionComponent();
        UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
        UStaticMesh* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
        auto SpawnBase=[&](FVector L){AStaticMeshActor* A=F.World->SpawnActor<AStaticMeshActor>(L,FRotator::ZeroRotator);A->GetStaticMeshComponent()->SetStaticMesh(Cube);return A;};
        AStaticMeshActor* OtherActor=SpawnBase(FVector(500,500,0));AStaticMeshActor* OtherActor2=SpawnBase(FVector(800,500,0));
        UActorComponent* Other=OtherActor->GetStaticMeshComponent();UActorComponent* Other2=OtherActor2->GetStaticMeshComponent();
        FScopedSubLevel Sub(F.World);if(!TestTrue(TEXT("sublevel"),Sub.Valid()))return false;
        auto& Cache=Sub.IFA->InstanceBaseCache;
        auto AddOn=[&](FFoliageInfo* Info,UFoliageType* Type,FVector L,UActorComponent* Base){FFoliageInstance I;I.Location=L;I.BaseComponent=Base;Info->AddInstance(Type,I,Base);};
        switch(C.Kind)
        {
        case EKind::TargetAttached:AddOn(Sub.Info,Sub.Type,FVector(500,600,0),Collision);break;
        case EKind::StaleTarget:{AddOn(Sub.Info,Sub.Type,FVector(500,600,0),Collision);const int32 R[1]={0};Sub.Info->RemoveInstances(R,true);break;}
        case EKind::MixedSafeFirst:
        {
            // FoliageType A: 安全な参照、続いて未解決の参照。FoliageType B: 安全な参照。
            AddOn(Sub.Info,Sub.Type,FVector(500,600,0),Other);AddOn(Sub.Info,Sub.Type,FVector(800,600,0),Other2);
            UFoliageType* TypeB=nullptr;FFoliageInfo* InfoB=Sub.IFA->AddMesh(Sphere,&TypeB);if(InfoB)AddOn(InfoB,TypeB,FVector(600,600,0),Other);
            Cache.InstanceBaseMap[Cache.GetInstanceBaseId(Other2)].BasePtr.Reset();break;
        }
        default:AddOn(Sub.Info,Sub.Type,FVector(500,600,0),Other);break;
        }
        const FFoliageInstanceBaseId Id=Cache.GetInstanceBaseId(Other);
        switch(C.Kind)
        {
        case EKind::EntryMissing:Cache.InstanceBaseMap.Remove(Id);break;
        case EKind::NullPtr:Cache.InstanceBaseMap[Id].BasePtr.Reset();break;
        case EKind::UnresolvablePath:Cache.InstanceBaseMap[Id].BasePtr=TSoftObjectPtr<UActorComponent>(FSoftObjectPath(TEXT("/Temp/LandscapeMCP_V05_Missing.LandscapeMCP_V05_Missing:PersistentLevel.MissingActor.MissingComponent")));break;
        case EKind::DestroyedComponent:
        {
            // base Actorを削除するとEditorが付属instanceも消すため、entryだけを破棄済み（garbage）のcomponentへ向ける。
            UStaticMeshComponent* Dead=NewObject<UStaticMeshComponent>(OtherActor);Dead->MarkAsGarbage();
            Cache.InstanceBaseMap[Id].BasePtr=Dead;break;
        }
        case EKind::HashMismatch:
        {
            // 別の有効なbaseを登録し、instanceのBaseIdだけをそれへ書き換える（ComponentHashは元のまま）。
            const FFoliageInstanceBaseId Id2=Cache.AddInstanceBaseId(Other2);Sub.Info->Instances[0].BaseId=Id2;break;
        }
        default:break;
        }
        if(C.Kind!=EKind::StaleTarget&&!TestTrue(FString(C.Name)+TEXT(": fixture instance exists"),Sub.Info->Instances.Num()>0))return false;
        if(C.Kind!=EKind::StaleTarget)
        {
            // 前提: 実instanceのBaseIdとComponentHashが対応している（不整合ケースは意図した1点だけ）。
            const FFoliageInstance& I0=Sub.Info->Instances[0];
            if(C.Kind!=EKind::HashMismatch)TestTrue(FString(C.Name)+TEXT(": BaseId and ComponentHash agree"),Sub.Info->ComponentHash.Contains(I0.BaseId)&&Sub.Info->ComponentHash[I0.BaseId].Contains(0));
            TestTrue(FString(C.Name)+TEXT(": valid base id in use"),I0.BaseId!=FFoliageInstanceBaseCache::InvalidBaseId);
        }
        Sub.Level->bIsVisible=false;
#if WITH_EDITORONLY_DATA
        Sub.Level->bLocked=true;
#endif
        UPackage* Package=F.World->GetOutermost();UPackage* SubPackage=Sub.Level->GetOutermost();
        Package->SetDirtyFlag(false);SubPackage->SetDirtyFlag(false);
        const auto Original=F.Read();const FFoliageState Before=Capture(Sub.Info);const FCacheState CacheBefore=CaptureCache(Sub.IFA);
        const int32 Undo=UToolsetLibrary::GetActiveUndoCount();const int32 Levels=F.World->GetLevels().Num();
        const FString Prefix=FString(C.Name)+TEXT(" / ");
        if(C.Expected==0)
        {
            for(const EOperation Op:{EOperation::Sculpt,EOperation::Smooth,EOperation::Flatten})
            {
                TestTrue(Prefix+OpName(Op)+TEXT(": dry run"),Edit(Brush(F.Path,Op,true)).bSuccess);
                const int32 UndoBeforeApply=UToolsetLibrary::GetActiveUndoCount();
                const auto A=Edit(Brush(F.Path,Op,false));TestTrue(Prefix+OpName(Op)+TEXT(": apply ")+A.Message,A.bSuccess);
                // 書き込みがあった（Transactionが増えた）場合だけ戻す。平坦な地面のSmoothは何も積まない。
                if(UToolsetLibrary::GetActiveUndoCount()>UndoBeforeApply)GEditor->UndoTransaction(false);
            }
            TestFalse(Prefix+TEXT("inventory: not attached"),FoliageInspect(F.Path,0,0,6300,6300,0).bTargetHasAttachedFoliage);
            continue;
        }
        const TCHAR* Reason=C.Expected==1?TEXT("Attached foliage is unsupported"):TEXT("cannot be ruled out");
        for(const EOperation Op:{EOperation::Sculpt,EOperation::Smooth,EOperation::Flatten})
        {
            const auto D=Edit(Brush(F.Path,Op,true));const auto A=Edit(Brush(F.Path,Op,false));
            TestFalse(Prefix+OpName(Op)+TEXT(": dry run refused"),D.bSuccess);TestFalse(Prefix+OpName(Op)+TEXT(": apply refused"),A.bSuccess);
            TestTrue(Prefix+OpName(Op)+TEXT(": reason ")+A.Message,A.Message.Contains(Reason));
        }
        // CollectFoliage側: 付属先不明を「付属なし」と誤認しない（snap modeが有効なら拒否側に倒れる）。
        TestTrue(Prefix+TEXT("inventory: attachment risk reported"),FoliageInspect(F.Path,0,0,6300,6300,0).bTargetHasAttachedFoliage);
        {
            FScopedSnapMode Scope(*this,EFoliageLandscapeSnappingMode::SnapEverywhere);
            TestEqual(Prefix+TEXT("snap mode dry run blocked"),Execute(F.Rect(100,true)).Foliage.Verdict,FString(TEXT("BLOCKED")));
        }
        TestTrue(Prefix+TEXT("heightmap unchanged"),F.Read().Heights==Original.Heights);
        TestTrue(Prefix+TEXT("instances/BaseId unchanged"),Capture(Sub.Info)==Before);
        TestTrue(Prefix+TEXT("base cache unchanged"),CaptureCache(Sub.IFA)==CacheBefore);
        TestEqual(Prefix+TEXT("undo history unchanged"),UToolsetLibrary::GetActiveUndoCount(),Undo);
        TestFalse(Prefix+TEXT("package not dirtied"),Package->IsDirty());TestFalse(Prefix+TEXT("sublevel package not dirtied"),SubPackage->IsDirty());
        TestFalse(Prefix+TEXT("still hidden"),bool(Sub.Level->bIsVisible));
#if WITH_EDITORONLY_DATA
        TestTrue(Prefix+TEXT("still locked"),bool(Sub.Level->bLocked));
#endif
        TestEqual(Prefix+TEXT("level count unchanged"),F.World->GetLevels().Num(),Levels);
    }
    return true;
}

// 円形ブラシ3 ToolのApplyは、Landscape Grassがある、またはmaterialの中を確かめられない場合に書き込まない（v0.5.0 RC2）。
// Dry-runはv0.4の3 Toolと同じくGrassを検査せず成功する。Grassのないmaterialは従来どおり書き込める。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05BrushGrass,"LandscapeMCP.V05.Resolve.BrushGrassState",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05BrushGrass::RunTest(const FString&)
{
    enum class EKind{GrassOutput,GrassInstance,GrassComponents,LayeredInstance,AttachedAndGrass,PlainCustom,Default};
    struct FCase{const TCHAR* Name;EKind Kind;bool bRefused;};
    const FCase Cases[]={
        {TEXT("grass output material"),EKind::GrassOutput,true},{TEXT("instance of grass material"),EKind::GrassInstance,true},
        {TEXT("grass instance components"),EKind::GrassComponents,true},{TEXT("material layers via instance (not inspectable)"),EKind::LayeredInstance,true},
        {TEXT("attached foliage and grass"),EKind::AttachedAndGrass,true},
        {TEXT("custom material without grass"),EKind::PlainCustom,false},{TEXT("default material"),EKind::Default,false}};
    for(const EOperation Op:{EOperation::Sculpt,EOperation::Smooth,EOperation::Flatten})
    for(const FCase& C:Cases)
    {
        const FString Name=FString(OpName(Op))+TEXT(" / ")+C.Name;
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
        // Smoothにも差が出るよう、Grassのないdefault materialのうちに凸を作る。
        FEditRequest Bump=Brush(F.Path,EOperation::Sculpt,false);Bump.RadiusCm=600;Bump.Strength=200;
        if(!TestTrue(Name+TEXT(": bump"),Edit(Bump).bSuccess))return false;
        UMaterial* GrassMaterial=NewObject<UMaterial>(GetTransientPackage());
        GrassMaterial->GetExpressionCollection().AddExpression(NewObject<UMaterialExpressionLandscapeGrassOutput>(GrassMaterial));
        switch(C.Kind)
        {
        case EKind::GrassOutput:F.Actor->LandscapeMaterial=GrassMaterial;break;
        case EKind::GrassInstance:{UMaterialInstanceConstant* MI=NewObject<UMaterialInstanceConstant>(GetTransientPackage());MI->Parent=GrassMaterial;F.Actor->LandscapeMaterial=MI;break;}
        case EKind::GrassComponents:F.Actor->FoliageComponents.Add(NewObject<UHierarchicalInstancedStaticMeshComponent>(F.Actor));break;
        case EKind::LayeredInstance:
        {
            UMaterial* Layered=NewObject<UMaterial>(GetTransientPackage());
            Layered->GetExpressionCollection().AddExpression(NewObject<UMaterialExpressionMaterialAttributeLayers>(Layered));
            UMaterialInstanceConstant* MI=NewObject<UMaterialInstanceConstant>(GetTransientPackage());MI->Parent=Layered;F.Actor->LandscapeMaterial=MI;break;
        }
        case EKind::AttachedAndGrass:F.Actor->LandscapeMaterial=GrassMaterial;F.Add(FVector(3000,3000,0));break;
        case EKind::PlainCustom:F.Actor->LandscapeMaterial=NewObject<UMaterial>(GetTransientPackage());break;
        case EKind::Default:break;
        }
        UMaterialInterface* const Material=F.Actor->LandscapeMaterial;const int32 GrassComponents=F.Actor->FoliageComponents.Num();
        UPackage* Package=F.World->GetOutermost();Package->SetDirtyFlag(false);
        const auto Original=F.Read();const TArray<FFoliageInstance> Foliage=F.Instances();const int32 Undo=UToolsetLibrary::GetActiveUndoCount();
        const auto Dry=Edit(Brush(F.Path,Op,true));
        if(C.Kind==EKind::AttachedAndGrass)
        {
            TestFalse(Name+TEXT(": dry run refused for the attached foliage"),Dry.bSuccess);
            TestTrue(Name+TEXT(": dry run reason ")+Dry.Message,Dry.Message.Contains(TEXT("Attached foliage is unsupported")));
        }
        else
        {
            TestTrue(Name+TEXT(": dry run ")+Dry.Message,Dry.bSuccess);TestTrue(Name+TEXT(": dry run plans a change"),Dry.ChangedSampleCount>0);
        }
        TestTrue(Name+TEXT(": dry run heightmap unchanged"),F.Read().Heights==Original.Heights);
        TestEqual(Name+TEXT(": dry run undo unchanged"),UToolsetLibrary::GetActiveUndoCount(),Undo);
        TestFalse(Name+TEXT(": dry run package not dirtied"),Package->IsDirty());
        const auto Apply=Edit(Brush(F.Path,Op,false));
        if(C.bRefused)
        {
            TestFalse(Name+TEXT(": apply refused"),Apply.bSuccess);
            TestTrue(Name+TEXT(": reason ")+Apply.Message,Apply.Message.Contains(C.Kind==EKind::AttachedAndGrass?TEXT("Attached foliage is unsupported"):TEXT("UNSUPPORTED_GRASS_STATE")));
            if(C.Kind==EKind::LayeredInstance)TestTrue(Name+TEXT(": not inspectable"),Apply.Message.Contains(TEXT("cannot be inspected")));
            TestTrue(Name+TEXT(": heightmap unchanged"),F.Read().Heights==Original.Heights);
            TestEqual(Name+TEXT(": undo history unchanged"),UToolsetLibrary::GetActiveUndoCount(),Undo);
            TestFalse(Name+TEXT(": package not dirtied"),Package->IsDirty());
        }
        else
        {
            TestTrue(Name+TEXT(": apply ")+Apply.Message,Apply.bSuccess);TestTrue(Name+TEXT(": written"),F.Read().Heights!=Original.Heights);
            TestEqual(Name+TEXT(": one transaction"),UToolsetLibrary::GetActiveUndoCount(),Undo+1);
            TestTrue(Name+TEXT(": undo"),GEditor->UndoTransaction(false));F.Actor->ForceUpdateLayersContent();
            TestTrue(Name+TEXT(": undo restores heights"),F.Read().Heights==Original.Heights);
        }
        TestTrue(Name+TEXT(": material unchanged"),F.Actor->LandscapeMaterial==Material);
        TestEqual(Name+TEXT(": grass components unchanged"),F.Actor->FoliageComponents.Num(),GrassComponents);
        TestTrue(Name+TEXT(": foliage unchanged"),SameTransforms(F.Instances(),Foliage));
        F.Actor->LandscapeMaterial=nullptr;F.Actor->FoliageComponents.Reset();
    }
    {
        // 書き込みが不要な場合（平らな地面のSmooth）は、Grassがあっても書き込まずに成功する（v0.3からの動作）。
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
        UMaterial* GrassMaterial=NewObject<UMaterial>(GetTransientPackage());
        GrassMaterial->GetExpressionCollection().AddExpression(NewObject<UMaterialExpressionLandscapeGrassOutput>(GrassMaterial));
        F.Actor->LandscapeMaterial=GrassMaterial;const int32 Undo=UToolsetLibrary::GetActiveUndoCount();const auto Original=F.Read();
        const auto Apply=Edit(Brush(F.Path,EOperation::Smooth,false));
        TestTrue(TEXT("flat smooth with grass: no write needed"),Apply.bSuccess&&Apply.ChangedSampleCount==0);
        TestEqual(TEXT("flat smooth with grass: no transaction"),UToolsetLibrary::GetActiveUndoCount(),Undo);
        TestTrue(TEXT("flat smooth with grass: heights unchanged"),F.Read().Heights==Original.Heights);
        F.Actor->LandscapeMaterial=nullptr;
    }
    return true;
}

// v0.5.0 RC3: Grassの判定に使うmaterialとmaterial functionのgraph、Grass判定から書き込みまでの間の状態変化。
namespace LandscapeMCP::FoliageTests
{
    // graphを作る。expressionにはEditorで作る場合と同じく、所有元へのback-pointerを付ける。
    UMaterial* NewMaterial(){return NewObject<UMaterial>(GetTransientPackage());}
    UMaterialFunction* NewFunction(){return NewObject<UMaterialFunction>(GetTransientPackage());}
    template<class TExpr> TExpr* AddTo(UMaterial* M){TExpr* E=NewObject<TExpr>(M);E->Material=M;M->GetExpressionCollection().AddExpression(E);return E;}
    template<class TExpr> TExpr* AddTo(UMaterialFunction* F){TExpr* E=NewObject<TExpr>(F);E->Function=F;F->GetExpressionCollection().AddExpression(E);return E;}
    template<class TOwner> void CallFrom(TOwner* Owner,UMaterialFunctionInterface* Callee){AddTo<UMaterialExpressionMaterialFunctionCall>(Owner)->MaterialFunction=Callee;}
    UMaterial* MaterialCalling(UMaterialFunctionInterface* Callee){UMaterial* M=NewMaterial();CallFrom(M,Callee);return M;}
    UMaterialFunction* GrassFunction(){UMaterialFunction* F=NewFunction();AddTo<UMaterialExpressionLandscapeGrassOutput>(F);return F;}
    UMaterialInstanceConstant* InstanceOf(UMaterialInterface* Parent){UMaterialInstanceConstant* I=NewObject<UMaterialInstanceConstant>(GetTransientPackage());I->Parent=Parent;return I;}
    UMaterialFunctionInstance* FunctionInstanceOf(UMaterialFunctionInterface* Parent)
    {UMaterialFunctionInstance* I=NewObject<UMaterialFunctionInstance>(GetTransientPackage());I->Parent=Parent;I->Base=Parent?Parent->GetBaseFunctionInterface():nullptr;return I;}
    // Material Layersの最初のslotにfunctionを入れる（UEの既定は空の背景layer）。
    UMaterial* LayeredMaterial(UMaterialFunctionInterface* Layer)
    {
        UMaterial* M=NewMaterial();auto* L=AddTo<UMaterialExpressionMaterialAttributeLayers>(M);
        if(Layer){if(L->DefaultLayers.Layers.IsEmpty())L->DefaultLayers.Layers.Add(Layer);else L->DefaultLayers.Layers[0]=Layer;}
        return M;
    }
    // editor dataのないmaterial・function（cook済みや破損したassetと同じ形）。
    bool DropEditorData(UObject* O)
    {
        UClass* Owner=O->IsA<UMaterialFunctionInterface>()?UMaterialFunctionInterface::StaticClass():UMaterialInterface::StaticClass();
        FObjectPropertyBase* P=FindFProperty<FObjectPropertyBase>(Owner,TEXT("EditorOnlyData"));
        if(!P)return false;P->SetObjectPropertyValue_InContainer(O,nullptr);return true;
    }
    // materialが解析で変わっていないことの目印（親、expressionの数、editor data）。親が循環するMaterial Instanceは
    // serializeが戻らない（UEの動作。修正前の再現で確認）ため、Hash()ではなくfieldを直接読む。
    uint64 Fingerprint(const UMaterialInterface* M)
    {
        if(const UMaterialInstance* I=Cast<UMaterialInstance>(M))return uint64(UPTRINT(I->Parent.Get()));
        const UMaterial* Base=Cast<UMaterial>(M);if(!Base)return 0;const UMaterialInterface* Data=Base;
        return HashCombine(GetTypeHash(Data->GetEditorOnlyData()),Data->GetEditorOnlyData()?GetTypeHash(Base->GetExpressions().Num()):0);
    }
    // 円形ブラシ3 ToolのApplyを、materialを差し替えたLandscapeで実行する。materialは凸を作った後に差し替える（Smoothにも差が出る）。
    // RefusedReasonがnullなら書き込みとUndoの成功、それ以外は書き込まずに拒否されることを確かめる。
    void BrushMaterialCase(FAutomationTestBase& Test,const FString& Case,TFunctionRef<UMaterialInterface*()> Make,const TCHAR* RefusedReason)
    {
        for(const EOperation Op:{EOperation::Sculpt,EOperation::Smooth,EOperation::Flatten})
        {
            const FString Name=FString(OpName(Op))+TEXT(" / ")+Case;
            FFixture F;if(!Test.TestTrue(Name+TEXT(": fixture"),F.Valid()))return;
            FEditRequest Bump=Brush(F.Path,EOperation::Sculpt,false);Bump.RadiusCm=600;Bump.Strength=200;
            if(!Test.TestTrue(Name+TEXT(": bump"),Edit(Bump).bSuccess))return;
            UMaterialInterface* const Material=Make();F.Actor->LandscapeMaterial=Material;const uint64 MaterialPrint=Fingerprint(Material);
            UPackage* Package=F.World->GetOutermost();Package->SetDirtyFlag(false);
            const auto Original=F.Read();const int32 Undo=UToolsetLibrary::GetActiveUndoCount();
            const auto Dry=Edit(Brush(F.Path,Op,true));
            Test.TestTrue(Name+TEXT(": dry run ")+Dry.Message,Dry.bSuccess&&Dry.ChangedSampleCount>0);
            const auto Apply=Edit(Brush(F.Path,Op,false));
            if(RefusedReason)
            {
                Test.TestFalse(Name+TEXT(": apply refused"),Apply.bSuccess);
                Test.TestTrue(Name+TEXT(": reason ")+Apply.Message,Apply.Message.StartsWith(TEXT("UNSUPPORTED_GRASS_STATE"))&&Apply.Message.Contains(RefusedReason));
                Test.TestTrue(Name+TEXT(": heightmap unchanged"),F.Read().Heights==Original.Heights);
                Test.TestEqual(Name+TEXT(": undo history unchanged"),UToolsetLibrary::GetActiveUndoCount(),Undo);
                Test.TestFalse(Name+TEXT(": package not dirtied"),Package->IsDirty());
            }
            else
            {
                Test.TestTrue(Name+TEXT(": apply ")+Apply.Message,Apply.bSuccess);Test.TestTrue(Name+TEXT(": written"),F.Read().Heights!=Original.Heights);
                Test.TestEqual(Name+TEXT(": one transaction"),UToolsetLibrary::GetActiveUndoCount(),Undo+1);
                Test.TestTrue(Name+TEXT(": undo"),GEditor->UndoTransaction(false));F.Actor->ForceUpdateLayersContent();
                Test.TestTrue(Name+TEXT(": undo restores heights"),F.Read().Heights==Original.Heights);
            }
            Test.TestTrue(Name+TEXT(": material unchanged"),F.Actor->LandscapeMaterial==Material&&Fingerprint(Material)==MaterialPrint);
            Test.TestTrue(Name+TEXT(": no grass components"),F.Actor->FoliageComponents.IsEmpty());
            F.Actor->LandscapeMaterial=nullptr;
        }
    }
    constexpr const TCHAR* NotInspectable=TEXT("cannot be inspected");
    constexpr const TCHAR* GrassPresent=TEXT("Landscape Grass Output is present");
    constexpr const TCHAR* LimitExceeded=TEXT("analysis limit exceeded");
    // rootのmaterialからLeaves個のfunctionを呼び、最後に長さChainの呼び出しの連鎖を呼ぶ。bTwiceなら各functionを2回呼ぶ（共有DAG）。
    // 解析するobjectの数は 1 + Leaves + Chain。
    UMaterial* Budget(int32 Leaves,int32 Chain,bool bTwice)
    {
        UMaterial* M=NewMaterial();TArray<UMaterialFunction*> List;
        for(int32 I=0;I<Leaves;++I){List.Add(NewFunction());CallFrom(M,List.Last());}
        if(bTwice)for(UMaterialFunction* F:List)CallFrom(M,F);
        if(Chain>0)
        {
            UMaterialFunction* Head=NewFunction();UMaterialFunction* Tail=Head;
            for(int32 I=1;I<Chain;++I){UMaterialFunction* Next=NewFunction();CallFrom(Tail,Next);Tail=Next;}
            CallFrom(M,Head);
        }
        return M;
    }
}
namespace LandscapeMCP::FoliageTests
{
    // M1の構成。IDの頭文字: U=判定不能、H=修正前のDLLで無限再帰・editor dataの欠損によりEditorが停止し得る判定不能、
    // G=Grassの検出（RC2で検出できていた形の回帰確認）、A=解析でき、Grassがない（拒否しない）。
    // 修正前の再現では、1件ずつ別processで実行できるよう、caseごとに名前を分ける（BrushMaterialGraph.<ID>）。
    struct FGraphCase{const TCHAR* Id;const TCHAR* Description;UMaterialInterface*(*Make)(FAutomationTestBase&);const TCHAR* Reason;};
    const TArray<FGraphCase>& GraphCases()
    {
        static const TArray<FGraphCase> Cases={
            {TEXT("U1_NullCall"),TEXT("function call without a function"),+[](FAutomationTestBase&)->UMaterialInterface*
                {UMaterial* M=NewMaterial();AddTo<UMaterialExpressionMaterialFunctionCall>(M);return M;},NotInspectable},
            {TEXT("U2_InstanceNoParent"),TEXT("material instance without a parent"),+[](FAutomationTestBase&)->UMaterialInterface*{return InstanceOf(nullptr);},NotInspectable},
            {TEXT("U3_InstanceCycle"),TEXT("material instance parent cycle"),+[](FAutomationTestBase&)->UMaterialInterface*
                {UMaterialInstanceConstant* A=InstanceOf(nullptr);A->Parent=InstanceOf(A);return A;},NotInspectable},
            {TEXT("U4_InstanceChainNoParent"),TEXT("instance of an instance without a parent"),+[](FAutomationTestBase&)->UMaterialInterface*{return InstanceOf(InstanceOf(nullptr));},NotInspectable},
            {TEXT("U5_FunctionInstanceNoParent"),TEXT("function instance without a parent"),+[](FAutomationTestBase&)->UMaterialInterface*{return MaterialCalling(FunctionInstanceOf(nullptr));},NotInspectable},
            {TEXT("U6_FunctionInstanceCycle"),TEXT("function instance parent cycle"),+[](FAutomationTestBase&)->UMaterialInterface*
                {UMaterialFunctionInstance* A=FunctionInstanceOf(nullptr);A->Parent=FunctionInstanceOf(A);return MaterialCalling(A);},NotInspectable},
            {TEXT("U7_NestedNullCall"),TEXT("nested call without a function"),+[](FAutomationTestBase&)->UMaterialInterface*
                {UMaterialFunction* F=NewFunction();AddTo<UMaterialExpressionMaterialFunctionCall>(F);return MaterialCalling(F);},NotInspectable},
            {TEXT("H1_CallCycle"),TEXT("function call cycle"),+[](FAutomationTestBase&)->UMaterialInterface*
                {UMaterialFunction* A=NewFunction();UMaterialFunction* B=NewFunction();CallFrom(A,B);CallFrom(B,A);return MaterialCalling(A);},NotInspectable},
            {TEXT("H2_SelfCall"),TEXT("function calling itself"),+[](FAutomationTestBase&)->UMaterialInterface*{UMaterialFunction* A=NewFunction();CallFrom(A,A);return MaterialCalling(A);},NotInspectable},
            {TEXT("H3_NestedNoEditorData"),TEXT("nested function without editor data"),+[](FAutomationTestBase& T)->UMaterialInterface*
                {UMaterialFunction* Inner=NewFunction();UMaterialFunction* Outer=NewFunction();CallFrom(Outer,Inner);T.TestTrue(TEXT("editor data dropped"),DropEditorData(Inner));return MaterialCalling(Outer);},NotInspectable},
            {TEXT("H4_FunctionNoEditorData"),TEXT("called function without editor data"),+[](FAutomationTestBase& T)->UMaterialInterface*
                {UMaterialFunction* F=NewFunction();T.TestTrue(TEXT("editor data dropped"),DropEditorData(F));return MaterialCalling(F);},NotInspectable},
            {TEXT("H5_MaterialNoEditorData"),TEXT("material without editor data"),+[](FAutomationTestBase& T)->UMaterialInterface*
                {UMaterial* M=NewMaterial();T.TestTrue(TEXT("editor data dropped"),DropEditorData(M));return M;},NotInspectable},
            {TEXT("G1_NestedGrass"),TEXT("grass in a nested function"),+[](FAutomationTestBase&)->UMaterialInterface*
                {UMaterialFunction* F=NewFunction();CallFrom(F,GrassFunction());return MaterialCalling(F);},GrassPresent},
            {TEXT("G2_SharedGrass"),TEXT("grass in a shared function"),+[](FAutomationTestBase&)->UMaterialInterface*
                {UMaterialFunction* S=GrassFunction();UMaterialFunction* A=NewFunction();UMaterialFunction* B=NewFunction();CallFrom(A,S);CallFrom(B,S);UMaterial* M=MaterialCalling(A);CallFrom(M,B);return M;},GrassPresent},
            {TEXT("G3_FunctionInstanceGrass"),TEXT("grass behind a function instance"),+[](FAutomationTestBase&)->UMaterialInterface*{return MaterialCalling(FunctionInstanceOf(GrassFunction()));},GrassPresent},
            {TEXT("G4_LayerGrass"),TEXT("grass in a layer function"),+[](FAutomationTestBase&)->UMaterialInterface*{return LayeredMaterial(GrassFunction());},GrassPresent},
            {TEXT("A1_NestedClean"),TEXT("nested functions without grass"),+[](FAutomationTestBase&)->UMaterialInterface*
                {UMaterialFunction* F=NewFunction();CallFrom(F,NewFunction());return MaterialCalling(F);},nullptr},
            {TEXT("A2_SharedDagClean"),TEXT("shared function DAG without grass"),+[](FAutomationTestBase&)->UMaterialInterface*
            {
                UMaterialFunction* S=NewFunction();UMaterialFunction* A=NewFunction();UMaterialFunction* B=NewFunction();
                CallFrom(A,S);CallFrom(A,S);CallFrom(B,S);UMaterial* M=MaterialCalling(A);CallFrom(M,B);CallFrom(M,S);return M;
            },nullptr},
            {TEXT("A3_InstanceChainClean"),TEXT("instance chain without grass"),+[](FAutomationTestBase&)->UMaterialInterface*{return InstanceOf(InstanceOf(NewMaterial()));},nullptr},
            {TEXT("A4_FunctionInstanceClean"),TEXT("function instance chain without grass"),+[](FAutomationTestBase&)->UMaterialInterface*
                {return MaterialCalling(FunctionInstanceOf(FunctionInstanceOf(NewFunction())));},nullptr},
            {TEXT("A5_LayersEmptySlot"),TEXT("material layers with an empty slot"),+[](FAutomationTestBase&)->UMaterialInterface*{return LayeredMaterial(nullptr);},nullptr},
            {TEXT("A6_LayersClean"),TEXT("material layers with a layer without grass"),+[](FAutomationTestBase&)->UMaterialInterface*{return LayeredMaterial(NewFunction());},nullptr},
            // RC4: 解析するmaterial・functionの数の上限（4,096）。数は解析中と解析済みの合計（rootのmaterialを含む）。共有された参照は数えない。
            {TEXT("B1_Objects4095"),TEXT("4,095 objects"),+[](FAutomationTestBase&)->UMaterialInterface*{return Budget(4094,0,false);},nullptr},
            {TEXT("B2_Objects4096"),TEXT("4,096 objects"),+[](FAutomationTestBase&)->UMaterialInterface*{return Budget(4095,0,false);},nullptr},
            {TEXT("B3_Objects4097"),TEXT("4,097 objects"),+[](FAutomationTestBase&)->UMaterialInterface*{return Budget(4096,0,false);},LimitExceeded},
            {TEXT("B4_TailChain4097"),TEXT("4,097 objects ending in a call chain"),+[](FAutomationTestBase&)->UMaterialInterface*{return Budget(4094,2,false);},LimitExceeded},
            {TEXT("B5_TailChain4096"),TEXT("4,096 objects ending in a call chain"),+[](FAutomationTestBase&)->UMaterialInterface*{return Budget(4093,2,false);},nullptr},
            {TEXT("B6_SharedDag4096"),TEXT("4,096 objects, each function called twice"),+[](FAutomationTestBase&)->UMaterialInterface*{return Budget(4095,0,true);},nullptr}};
        return Cases;
    }
}
// M1: 列挙結果が空なだけでは「Grassなし」としない。未解決の参照・循環・editor dataの欠損は判定不能として拒否し、
// 解析できたGrassのないgraph（入れ子、共有DAG、instance、空のlayer slot）は拒否しない。
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FV05BrushMaterialGraph,"LandscapeMCP.V05.Resolve.BrushMaterialGraph",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FV05BrushMaterialGraph::GetTests(TArray<FString>& OutBeautifiedNames,TArray<FString>& OutTestCommands)const
{
    for(const FGraphCase& C:GraphCases()){OutBeautifiedNames.Add(C.Id);OutTestCommands.Add(C.Id);}
}
bool FV05BrushMaterialGraph::RunTest(const FString& Parameters)
{
    for(const FGraphCase& C:GraphCases())
        if(Parameters==C.Id){BrushMaterialCase(*this,C.Description,[&]{return C.Make(*this);},C.Reason);return true;}
    AddError(TEXT("Unknown case: ")+Parameters);return false;
}

namespace LandscapeMCP::FoliageTests
{
    // UObject::Modify()の通知で同期実行されるlistener（material等を自動で変えるEditor拡張の代わり）。条件に合う最初の通知で1回だけ状態を変える。
    struct FModifyListener
    {
        FDelegateHandle Handle;int32 Fired=0;
        FModifyListener(TFunction<bool(UObject*)> Match,TFunction<void()> Change)
        {
            Handle=FCoreUObjectDelegates::OnObjectModified.AddLambda([this,Match=MoveTemp(Match),Change=MoveTemp(Change)](UObject* O)
                {if(Fired==0&&Match(O)){++Fired;Change();}});
        }
        ~FModifyListener(){FCoreUObjectDelegates::OnObjectModified.Remove(Handle);}
    };
    UMaterial* GrassMaterial(){UMaterial* M=NewMaterial();AddTo<UMaterialExpressionLandscapeGrassOutput>(M);return M;}
}
// M2: Grass判定の後、Actor/InfoのModify通知や書き込み中の通知でmaterial・Grassの状態が変わった場合は、
// 書き込まない（書き込み前）か、所有するtransactionをUndoして成功を返さない（書き込み後）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV05BrushGrassReentry,"LandscapeMCP.V05.Resolve.BrushGrassReentry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV05BrushGrassReentry::RunTest(const FString&)
{
    enum class ETrigger{Actor,Info,DuringWrite};
    enum class EChange{None,GrassMaterial,GrassInPlace,ComponentOverride,GrassComponents};
    struct FCase{const TCHAR* Name;ETrigger Trigger;EChange Change;bool bRestoreFault=false;};
    const FCase Cases[]={
        {TEXT("Actor.Modify sets a grass material"),ETrigger::Actor,EChange::GrassMaterial},
        {TEXT("Actor.Modify adds grass output to the same material"),ETrigger::Actor,EChange::GrassInPlace},
        {TEXT("Info.Modify sets a component override with grass"),ETrigger::Info,EChange::ComponentOverride},
        {TEXT("Info.Modify adds grass instance components"),ETrigger::Info,EChange::GrassComponents},
        {TEXT("during the write: grass material"),ETrigger::DuringWrite,EChange::GrassMaterial},
        {TEXT("during the write: grass instance components"),ETrigger::DuringWrite,EChange::GrassComponents},
        {TEXT("during the write: grass material, restoration not provable (fault)"),ETrigger::DuringWrite,EChange::GrassMaterial,true},
        {TEXT("Actor.Modify without a change (control)"),ETrigger::Actor,EChange::None}};
    for(const EOperation Op:{EOperation::Sculpt,EOperation::Smooth,EOperation::Flatten})
    for(const FCase& C:Cases)
    {
        const FString Name=FString(OpName(Op))+TEXT(" / ")+C.Name;
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
        FEditRequest Bump=Brush(F.Path,EOperation::Sculpt,false);Bump.RadiusCm=600;Bump.Strength=200;
        if(!TestTrue(Name+TEXT(": bump"),Edit(Bump).bSuccess))return false;
        // Grassのないcustom material（Apply可）から始める。
        UMaterial* const Plain=NewMaterial();F.Actor->LandscapeMaterial=Plain;
        ALandscape* const Actor=F.Actor;ULandscapeInfo* const Info=Actor->GetLandscapeInfo();
        UPackage* Package=F.World->GetOutermost();Package->SetDirtyFlag(false);
        const auto Original=F.Read();const int32 Undo=UToolsetLibrary::GetActiveUndoCount();
        UMaterialInterface* Expected=Plain;
        auto Change=[&]()
        {
            switch(C.Change)
            {
            case EChange::None:break;
            case EChange::GrassMaterial:Expected=GrassMaterial();Actor->LandscapeMaterial=Expected;break;
            case EChange::GrassInPlace:AddTo<UMaterialExpressionLandscapeGrassOutput>(Plain);break;
            case EChange::ComponentOverride:Actor->LandscapeComponents[0]->OverrideMaterial=GrassMaterial();break;
            case EChange::GrassComponents:Actor->FoliageComponents.Add(NewObject<UHierarchicalInstancedStaticMeshComponent>(Actor));break;
            }
        };
        // 書き込み中: InfoのModify（書き込み前の最後のModify）の後に来る、Landscapeの部品（componentやheightmap）のModify通知。
        // Actor->Modify()はroot componentもModifyするので、Actorの後ではなくInfoの後から数える。
        bool bArmed=false;TArray<FString> Seen;
        FModifyListener Listener([&](UObject* O)
        {
            if(O==Actor||O==Info){bArmed|=O==Info;return C.Trigger==(O==Actor?ETrigger::Actor:ETrigger::Info);}
            Seen.Add(O->GetClass()->GetName());
            return C.Trigger==ETrigger::DuringWrite&&bArmed&&O->IsIn(Actor);
        },Change);
        // UEは同じframeの2回目以降のModify通知を送らない（ObjectsModifiedThisFrame）。Testは1 frameの中で凸を作った直後なので、
        // Engineがframeの終わりに行うのと同じく集合を空にして、次のframeでのApplyと同じ通知の状態にする。
        FCoreUObjectDelegates::ObjectsModifiedThisFrame.Empty();
        SetAutomationBrushRestoreFailure(C.bRestoreFault);
        const auto Apply=Edit(Brush(F.Path,Op,false));
        SetAutomationBrushRestoreFailure(false);
        AddInfo(Name+TEXT(": modified during apply: ")+FString::Join(Seen,TEXT(","))+TEXT(" / ")+Apply.Message);
        TestEqual(Name+TEXT(": listener fired"),Listener.Fired,1);
        if(C.Change==EChange::None)
        {
            TestTrue(Name+TEXT(": apply ")+Apply.Message,Apply.bSuccess);TestTrue(Name+TEXT(": written"),F.Read().Heights!=Original.Heights);
            TestEqual(Name+TEXT(": one transaction"),UToolsetLibrary::GetActiveUndoCount(),Undo+1);
            TestTrue(Name+TEXT(": undo"),GEditor->UndoTransaction(false));Actor->ForceUpdateLayersContent();
            TestTrue(Name+TEXT(": undo restores heights"),F.Read().Heights==Original.Heights);
        }
        else
        {
            TestFalse(Name+TEXT(": apply refused"),Apply.bSuccess);
            TestTrue(Name+TEXT(": reason ")+Apply.Message,Apply.Message.StartsWith(TEXT("UNSUPPORTED_GRASS_STATE")));
            TestTrue(Name+TEXT(": heightmap unchanged or restored"),F.Read().Heights==Original.Heights);
            TestEqual(Name+TEXT(": no undo record left"),UToolsetLibrary::GetActiveUndoCount(),Undo);
            if(C.Trigger!=ETrigger::DuringWrite)
            {
                TestTrue(Name+TEXT(": nothing written"),Apply.Message.Contains(TEXT("nothing was written")));
                TestFalse(Name+TEXT(": package not dirtied"),Package->IsDirty());
            }
            else if(!C.bRestoreFault)TestTrue(Name+TEXT(": rolled back"),Apply.Message.Contains(TEXT("rollback completed")));
            else TestTrue(Name+TEXT(": rollback not provable"),Apply.Message.Contains(TEXT("rollback FAILED")));
            if(C.Trigger!=ETrigger::DuringWrite)
            {
                // 書き込む前の拒否: このToolは、listenerが変えた状態をさらに変えたり、元に戻したりしない。
                TestTrue(Name+TEXT(": material is the listener's"),Actor->LandscapeMaterial==Expected);
            }
            else
            {
                // 書き込み後のrollback: ActorはこのToolのtransactionに記録済みなので、そのtransactionの中でlistenerが
                // Actorに加えた変更も、UndoでApply前の状態に戻る（UEのUndoの動作）。Grassのcomponentも残らない。
                TestTrue(Name+TEXT(": actor restored by undo"),Actor->LandscapeMaterial==Plain);
                TestTrue(Name+TEXT(": no grass components after undo"),Actor->FoliageComponents.IsEmpty());
            }
        }
        // 後片付け。書き込み後に復元を証明できなかった場合は、後続の円形ブラシの書き込みも止まっている。
        Actor->LandscapeMaterial=Plain;Plain->GetExpressionCollection().Empty();Actor->LandscapeComponents[0]->OverrideMaterial=nullptr;Actor->FoliageComponents.Reset();
        const auto Next=Edit(Brush(F.Path,Op,false));
        if(C.bRestoreFault)
        {
            TestFalse(Name+TEXT(": later brush writes stopped"),Next.bSuccess);
            TestTrue(Name+TEXT(": latch reason ")+Next.Message,Next.Message.StartsWith(TEXT("BRUSH_WRITES_DISABLED")));
            ResetAutomationApplyState();
        }
        else if(C.Change!=EChange::None)TestTrue(Name+TEXT(": later write allowed ")+Next.Message,Next.bSuccess);
        if(Next.bSuccess)GEditor->UndoTransaction(false);
        Actor->LandscapeMaterial=nullptr;
    }
    // v0.4の3 Tool（FlattenRectangleで代表）: 書き込み直前の再検証。v0.4のApplyはdefault materialだけを許す。
    for(const bool bChange:{true,false})
    {
        const FString Name=bChange?TEXT("FlattenRectangle / Actor.Modify sets a grass material"):TEXT("FlattenRectangle / Actor.Modify without a change (control)");
        FFixture F;if(!TestTrue(TEXT("fixture"),F.Valid()))return false;
        ALandscape* const Actor=F.Actor;UPackage* Package=F.World->GetOutermost();Package->SetDirtyFlag(false);
        const auto Original=F.Read();const int32 Undo=UToolsetLibrary::GetActiveUndoCount();UMaterial* Grass=GrassMaterial();
        FModifyListener Listener([&](UObject* O){return O==Actor;},[&]{if(bChange)Actor->LandscapeMaterial=Grass;});
        FCoreUObjectDelegates::ObjectsModifiedThisFrame.Empty();   // 次のframeと同じ通知の状態（上を参照）
        FString Stage;const auto O=Execute(F.Rect(100,false),&Stage);
        TestEqual(Name+TEXT(": listener fired"),Listener.Fired,1);
        if(bChange)
        {
            TestFalse(Name+TEXT(": refused"),O.bSuccess);
            TestEqual(Name+TEXT(": code ")+O.Message,O.ErrorCode,FString(TEXT("UNSUPPORTED_GRASS_STATE")));
            TestEqual(Name+TEXT(": stage"),Stage,FString(TEXT("transaction")));
            TestEqual(Name+TEXT(": no rollback needed"),O.RollbackStatus,FString(TEXT("NOT_NEEDED")));
            TestTrue(Name+TEXT(": heightmap unchanged"),F.Read().Heights==Original.Heights);
            TestEqual(Name+TEXT(": undo history unchanged"),UToolsetLibrary::GetActiveUndoCount(),Undo);
            TestFalse(Name+TEXT(": package not dirtied"),Package->IsDirty());
            TestFalse(Name+TEXT(": mutation still enabled"),MutationDisabled());
        }
        else
        {
            TestTrue(Name+TEXT(": applied ")+O.Message,O.bSuccess&&O.Status==TEXT("APPLIED"));
            TestTrue(Name+TEXT(": package dirtied by the write"),Package->IsDirty());
        }
        Actor->LandscapeMaterial=nullptr;
    }
    return true;
}

// RC4（M2の残り）: 書き込みの前後に同期で送られる通知（PackageMarkedDirtyEvent、書き込み中のModify）で
// material・graphが変わった場合の、rollbackの判定とlatch。復元を確かめられたときだけ「rollback completed」とし、
// listenerがtransactionの外で行った変更が残る場合は「rollback FAILED」で、円形ブラシ3 Toolの書き込みを止める。
namespace LandscapeMCP::FoliageTests
{
    // UPackage::PackageMarkedDirtyEvent（MarkPackageDirtyのたびに同期で送られる）で、対象packageの最初の通知で1回だけ状態を変える。
    struct FDirtyListener
    {
        FDelegateHandle Handle;int32 Fired=0;
        FDirtyListener(UPackage* Target,TFunction<void()> Change)
        {
            Handle=UPackage::PackageMarkedDirtyEvent.AddLambda([this,Target,Change=MoveTemp(Change)](UPackage* P,bool)
                {if(Fired==0&&P==Target){++Fired;Change();}});
        }
        ~FDirtyListener(){UPackage::PackageMarkedDirtyEvent.Remove(Handle);}
    };
    enum class ERollTrigger{None,DirtyEvent,DuringWrite};
    enum class ERollChange{None,GrassMaterial,GrassInPlace,NestedGrassInPlace,NestedCleanEdit};
    enum class ERollExpect{Applied,RolledBack,RollbackFailed,Unowned};
    struct FRollbackCase{const TCHAR* Id;const TCHAR* Description;ERollTrigger Trigger;ERollChange Change;ERollExpect Expect;bool bUnrelated;};
    const TArray<FRollbackCase>& RollbackCases()
    {
        static const TArray<FRollbackCase> Cases={
            {TEXT("P1_DirtyEventSwap"),TEXT("PackageMarkedDirtyEvent sets a grass material"),ERollTrigger::DirtyEvent,ERollChange::GrassMaterial,ERollExpect::RolledBack,false},
            {TEXT("P2_DirtyEventGrassInPlace"),TEXT("PackageMarkedDirtyEvent adds grass output to the same material"),ERollTrigger::DirtyEvent,ERollChange::GrassInPlace,ERollExpect::RollbackFailed,false},
            {TEXT("P3_DirtyEventNestedGrass"),TEXT("PackageMarkedDirtyEvent adds grass output to a nested function"),ERollTrigger::DirtyEvent,ERollChange::NestedGrassInPlace,ERollExpect::RollbackFailed,false},
            {TEXT("D1_DuringWriteGrassInPlace"),TEXT("during the write: grass output added to the same material"),ERollTrigger::DuringWrite,ERollChange::GrassInPlace,ERollExpect::RollbackFailed,false},
            {TEXT("D2_DuringWriteNestedGrass"),TEXT("during the write: grass output added to a nested function"),ERollTrigger::DuringWrite,ERollChange::NestedGrassInPlace,ERollExpect::RollbackFailed,false},
            {TEXT("D3_DuringWriteNestedCleanEdit"),TEXT("during the write: a nested function graph changes without grass"),ERollTrigger::DuringWrite,ERollChange::NestedCleanEdit,ERollExpect::RollbackFailed,false},
            {TEXT("D4_DuringWriteSwap"),TEXT("during the write: grass material on the actor (restored by the owned Undo)"),ERollTrigger::DuringWrite,ERollChange::GrassMaterial,ERollExpect::RolledBack,false},
            {TEXT("O1_UnownedLatest"),TEXT("during the write: grass material, latest transaction is not owned"),ERollTrigger::DuringWrite,ERollChange::GrassMaterial,ERollExpect::Unowned,true},
            {TEXT("N1_ApplyUndoRedo"),TEXT("no change: apply, undo, redo"),ERollTrigger::None,ERollChange::None,ERollExpect::Applied,false}};
        return Cases;
    }
    // 復元の照合に使う、テスト側の目印（Actorのmaterial、各graphのexpressionの数とGrass Outputの有無）。
    struct FGraphPrint
    {
        const UObject* Material=nullptr;TArray<int32> Counts;TArray<bool> Grass;
        bool operator==(const FGraphPrint& O)const{return Material==O.Material&&Counts==O.Counts&&Grass==O.Grass;}
    };
    template<class T> bool HasGrass(const T* O){for(const UMaterialExpression* E:O->GetExpressions())if(E&&E->IsA<UMaterialExpressionLandscapeGrassOutput>())return true;return false;}
    FGraphPrint PrintOf(const ALandscape* A,const UMaterial* Root,const UMaterialFunction* Outer,const UMaterialFunction* Inner)
    {
        FGraphPrint P;P.Material=A->LandscapeMaterial;
        P.Counts={Root->GetExpressions().Num(),Outer->GetExpressions().Num(),Inner->GetExpressions().Num()};
        P.Grass={HasGrass(Root),HasGrass(Outer),HasGrass(Inner)};return P;
    }
    void RollbackCase(FAutomationTestBase& Test,const FRollbackCase& C,bool bDirtyStart)
    {
        for(const EOperation Op:{EOperation::Sculpt,EOperation::Smooth,EOperation::Flatten})
        {
            const FString Name=FString(OpName(Op))+TEXT(" / ")+C.Description+(bDirtyStart?TEXT(" / dirty start"):TEXT(" / clean start"));
            FFixture F;if(!Test.TestTrue(Name+TEXT(": fixture"),F.Valid()))return;
            FEditRequest Bump=Brush(F.Path,EOperation::Sculpt,false);Bump.RadiusCm=600;Bump.Strength=200;
            if(!Test.TestTrue(Name+TEXT(": bump"),Edit(Bump).bSuccess))return;
            // Grassのない入れ子のgraph（material → Outer → Inner）から始める。
            UMaterialFunction* const Inner=NewFunction();UMaterialFunction* const Outer=NewFunction();CallFrom(Outer,Inner);
            UMaterial* const Plain=MaterialCalling(Outer);
            ALandscape* const Actor=F.Actor;ULandscapeInfo* const Info=Actor->GetLandscapeInfo();Actor->LandscapeMaterial=Plain;
            UPackage* const Package=Actor->GetPackage();Package->SetDirtyFlag(bDirtyStart);
            const auto Original=F.Read();const int32 Undo=UToolsetLibrary::GetActiveUndoCount();
            const FGuid UndoBefore=GEditor->Trans->GetUndoContext(false).TransactionId;
            const FGraphPrint Before=PrintOf(Actor,Plain,Outer,Inner);
            auto Change=[&]()
            {
                switch(C.Change)
                {
                case ERollChange::None:break;
                case ERollChange::GrassMaterial:Actor->LandscapeMaterial=GrassMaterial();break;
                case ERollChange::GrassInPlace:AddTo<UMaterialExpressionLandscapeGrassOutput>(Plain);break;   // Modifyもtransactionへの記録もしない変更
                case ERollChange::NestedGrassInPlace:AddTo<UMaterialExpressionLandscapeGrassOutput>(Inner);break;
                case ERollChange::NestedCleanEdit:CallFrom(Inner,NewFunction());break;
                }
            };
            TUniquePtr<FDirtyListener> Dirty;TUniquePtr<FModifyListener> Modified;bool bArmed=false;
            if(C.Trigger==ERollTrigger::DirtyEvent)Dirty=MakeUnique<FDirtyListener>(Package,Change);
            if(C.Trigger==ERollTrigger::DuringWrite)
                Modified=MakeUnique<FModifyListener>([&](UObject* O){if(O==Actor||O==Info){bArmed|=O==Info;return false;}return bArmed&&O->IsIn(Actor);},Change);
            FCoreUObjectDelegates::ObjectsModifiedThisFrame.Empty();   // 次のframeと同じ通知の状態（BrushGrassReentryを参照）
            SetAutomationBrushUnrelatedTransaction(C.bUnrelated);
            const auto Apply=Edit(Brush(F.Path,Op,false));
            SetAutomationBrushUnrelatedTransaction(false);
            const int32 Fired=Dirty?Dirty->Fired:Modified?Modified->Fired:0;Dirty.Reset();Modified.Reset();
            Test.AddInfo(Name+TEXT(": ")+Apply.Message);
            Test.TestEqual(Name+TEXT(": listener fired"),Fired,C.Trigger==ERollTrigger::None?0:1);
            const FGraphPrint After=PrintOf(Actor,Plain,Outer,Inner);
            switch(C.Expect)
            {
            case ERollExpect::Applied:
            {
                Test.TestTrue(Name+TEXT(": apply ")+Apply.Message,Apply.bSuccess);
                const auto Written=F.Read();Test.TestTrue(Name+TEXT(": written"),Written.Heights!=Original.Heights);
                Test.TestEqual(Name+TEXT(": one transaction"),UToolsetLibrary::GetActiveUndoCount(),Undo+1);
                Test.TestTrue(Name+TEXT(": package dirtied by the write"),Package->IsDirty());
                Test.TestTrue(Name+TEXT(": undo"),GEditor->UndoTransaction(true));Actor->ForceUpdateLayersContent();   // Redoできる通常のUndo
                Test.TestTrue(Name+TEXT(": undo restores heights"),F.Read().Heights==Original.Heights);
                Test.TestEqual(Name+TEXT(": undo restores dirty state"),Package->IsDirty(),bDirtyStart);
                Test.TestEqual(Name+TEXT(": undo restores undo history"),UToolsetLibrary::GetActiveUndoCount(),Undo);
                Test.TestTrue(Name+TEXT(": undo restores latest undo record"),GEditor->Trans->GetUndoContext(false).TransactionId==UndoBefore);
                Test.TestTrue(Name+TEXT(": redo"),GEditor->RedoTransaction());Actor->ForceUpdateLayersContent();
                Test.TestTrue(Name+TEXT(": redo restores the edit"),F.Read().Heights==Written.Heights);
                Test.TestEqual(Name+TEXT(": redo restores the transaction"),UToolsetLibrary::GetActiveUndoCount(),Undo+1);
                Test.TestTrue(Name+TEXT(": redo dirties the package"),Package->IsDirty());
                Test.TestTrue(Name+TEXT(": material graph unchanged"),After==Before);
                GEditor->UndoTransaction(false);
                break;
            }
            case ERollExpect::RolledBack:
                Test.TestFalse(Name+TEXT(": apply refused"),Apply.bSuccess);
                Test.TestTrue(Name+TEXT(": reason ")+Apply.Message,Apply.Message.StartsWith(TEXT("UNSUPPORTED_GRASS_STATE"))&&Apply.Message.Contains(TEXT("rollback completed")));
                Test.TestTrue(Name+TEXT(": heights restored"),F.Read().Heights==Original.Heights);
                Test.TestEqual(Name+TEXT(": undo history restored"),UToolsetLibrary::GetActiveUndoCount(),Undo);
                Test.TestTrue(Name+TEXT(": latest undo record restored"),GEditor->Trans->GetUndoContext(false).TransactionId==UndoBefore);
                Test.TestFalse(Name+TEXT(": refused write cannot be redone"),GEditor->Trans->CanRedo());
                Test.TestEqual(Name+TEXT(": dirty state restored"),Package->IsDirty(),bDirtyStart);
                Test.TestTrue(Name+TEXT(": material and graph restored by the owned Undo"),After==Before);
                Test.TestTrue(Name+TEXT(": no grass components"),Actor->FoliageComponents.IsEmpty());
                {
                    const auto Next=Edit(Brush(F.Path,Op,false));
                    Test.TestTrue(Name+TEXT(": later brush write allowed ")+Next.Message,Next.bSuccess);
                    if(Next.bSuccess)GEditor->UndoTransaction(false);
                }
                break;
            case ERollExpect::RollbackFailed:
            case ERollExpect::Unowned:
            {
                Test.TestFalse(Name+TEXT(": apply refused"),Apply.bSuccess);
                Test.TestTrue(Name+TEXT(": reason ")+Apply.Message,Apply.Message.StartsWith(TEXT("UNSUPPORTED_GRASS_STATE"))&&Apply.Message.Contains(TEXT("rollback FAILED")));
                if(C.Expect==ERollExpect::Unowned)
                {
                    // 最新の記録は自分のものではないので、Undoしない（高さは書き込まれたまま。Undoの記録は自分のものと無関係の1件）。
                    Test.TestTrue(Name+TEXT(": ownership not proved"),Apply.Message.Contains(TEXT("Ownership of the latest transaction could not be proved")));
                    Test.TestTrue(Name+TEXT(": no undo attempted"),F.Read().Heights!=Original.Heights);
                    Test.TestEqual(Name+TEXT(": own and unrelated transactions remain"),UToolsetLibrary::GetActiveUndoCount(),Undo+2);
                    Test.TestTrue(Name+TEXT(": latest record is the unrelated one"),GEditor->Trans->GetUndoContext(false).Context.StartsWith(TEXT("LandscapeMCP.Automation.")));
                }
                else
                {
                    // 自分のtransactionはUndoした。listenerがtransactionの外で加えたgraphの変更は、このToolでは戻さずに残す。
                    Test.TestTrue(Name+TEXT(": heights restored"),F.Read().Heights==Original.Heights);
                    Test.TestEqual(Name+TEXT(": undo history restored"),UToolsetLibrary::GetActiveUndoCount(),Undo);
                    Test.TestTrue(Name+TEXT(": latest undo record restored"),GEditor->Trans->GetUndoContext(false).TransactionId==UndoBefore);
                    Test.TestFalse(Name+TEXT(": refused write cannot be redone"),GEditor->Trans->CanRedo());
                    Test.TestEqual(Name+TEXT(": dirty state restored"),Package->IsDirty(),bDirtyStart);
                    Test.TestTrue(Name+TEXT(": actor material restored"),Actor->LandscapeMaterial==Plain);
                    Test.TestFalse(Name+TEXT(": the listener's graph change is left as is"),After==Before);
                }
                // latch: 次の円形ブラシの書き込みは、3 Toolとも書き込まずに拒否する。
                const auto Latched=F.Read();const int32 LatchedUndo=UToolsetLibrary::GetActiveUndoCount();const bool bLatchedDirty=Package->IsDirty();
                for(const EOperation Next:{EOperation::Sculpt,EOperation::Smooth,EOperation::Flatten})
                {
                    const FString NextName=Name+TEXT(" / then ")+OpName(Next);
                    const auto R=Edit(Brush(F.Path,Next,false));
                    Test.TestFalse(NextName+TEXT(": refused"),R.bSuccess);
                    Test.TestTrue(NextName+TEXT(": latch reason ")+R.Message,R.Message.StartsWith(TEXT("BRUSH_WRITES_DISABLED")));
                    Test.TestTrue(NextName+TEXT(": heights unchanged"),F.Read().Heights==Latched.Heights);
                    Test.TestEqual(NextName+TEXT(": undo unchanged"),UToolsetLibrary::GetActiveUndoCount(),LatchedUndo);
                    Test.TestEqual(NextName+TEXT(": dirty unchanged"),Package->IsDirty(),bLatchedDirty);
                    const auto Dry=Edit(Brush(F.Path,Next,true));
                    Test.TestTrue(NextName+TEXT(": dry run still allowed"),Dry.bSuccess);
                }
                ResetAutomationApplyState();
                break;
            }
            }
            Actor->LandscapeMaterial=nullptr;Actor->FoliageComponents.Reset();
        }
    }
    // v0.4の3 Tool（FlattenRectangleで代表）: PackageMarkedDirtyEventでの変化は書き込み後の検証で検出し、
    // 所有するtransactionをUndoして、Undo後の状態（Grassを含む）を確かめる（v0.4の既存の契約）。
    void V04DirtyEventCase(FAutomationTestBase& Test,bool bInPlace,bool bDirtyStart)
    {
        const FString Name=FString(bInPlace?TEXT("FlattenRectangle / PackageMarkedDirtyEvent adds grass output to the default material")
            :TEXT("FlattenRectangle / PackageMarkedDirtyEvent sets a grass material"))+(bDirtyStart?TEXT(" / dirty start"):TEXT(" / clean start"));
        FFixture F;if(!Test.TestTrue(Name+TEXT(": fixture"),F.Valid()))return;
        ALandscape* const Actor=F.Actor;UPackage* const Package=Actor->GetPackage();Package->SetDirtyFlag(bDirtyStart);
        UMaterial* const Default=UMaterial::GetDefaultMaterial(MD_Surface);
        const auto Original=F.Read();const int32 Undo=UToolsetLibrary::GetActiveUndoCount();
        UMaterialExpressionLandscapeGrassOutput* Added=nullptr;
        TUniquePtr<FDirtyListener> Listener=MakeUnique<FDirtyListener>(Package,[&]
        {
            if(!bInPlace){Actor->LandscapeMaterial=GrassMaterial();return;}
            Added=NewObject<UMaterialExpressionLandscapeGrassOutput>(Default);Added->Material=Default;Default->GetExpressionCollection().AddExpression(Added);
        });
        FString Stage;const auto O=Execute(F.Rect(100,false),&Stage);
        Test.TestEqual(Name+TEXT(": listener fired"),Listener->Fired,1);Listener.Reset();
        Test.AddInfo(Name+TEXT(": ")+O.ErrorCode+TEXT(" / ")+Stage+TEXT(" / ")+O.Message);
        Test.TestFalse(Name+TEXT(": not applied"),O.bSuccess);
        Test.TestTrue(Name+TEXT(": heights restored"),F.Read().Heights==Original.Heights);
        Test.TestEqual(Name+TEXT(": undo history restored"),UToolsetLibrary::GetActiveUndoCount(),Undo);
        Test.TestEqual(Name+TEXT(": dirty state restored"),Package->IsDirty(),bDirtyStart);
        Test.TestTrue(Name+TEXT(": actor material restored"),Actor->LandscapeMaterial==nullptr||Actor->GetLandscapeMaterial()==Default);
        if(bInPlace)
        {
            Test.TestEqual(Name+TEXT(": restoration not provable"),O.RollbackStatus,FString(TEXT("FAILED")));
            Test.TestTrue(Name+TEXT(": v0.4 mutation disabled"),MutationDisabled());
        }
        else
        {
            Test.TestEqual(Name+TEXT(": restoration verified"),O.RollbackStatus,FString(TEXT("VERIFIED")));
            Test.TestFalse(Name+TEXT(": v0.4 mutation still enabled"),MutationDisabled());
        }
        if(Added)Default->GetExpressionCollection().RemoveExpression(Added);
        Test.TestFalse(Name+TEXT(": default material cleaned up"),HasGrass(Default));
        ResetAutomationApplyState();Actor->LandscapeMaterial=nullptr;
    }
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FV05BrushRollbackProof,"LandscapeMCP.V05.Resolve.BrushRollbackProof",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FV05BrushRollbackProof::GetTests(TArray<FString>& OutBeautifiedNames,TArray<FString>& OutTestCommands)const
{
    for(const TCHAR* Start:{TEXT("Clean"),TEXT("Dirty")})
    {
        for(const FRollbackCase& C:RollbackCases()){const FString Id=FString(C.Id)+TEXT(".")+Start;OutBeautifiedNames.Add(Id);OutTestCommands.Add(Id);}
        for(const TCHAR* V:{TEXT("V1_V04DirtyEventSwap"),TEXT("V2_V04DirtyEventDefaultGrassInPlace")}){const FString Id=FString(V)+TEXT(".")+Start;OutBeautifiedNames.Add(Id);OutTestCommands.Add(Id);}
    }
}
bool FV05BrushRollbackProof::RunTest(const FString& Parameters)
{
    FString Id,Start;
    if(!Parameters.Split(TEXT("."),&Id,&Start)||(Start!=TEXT("Clean")&&Start!=TEXT("Dirty"))){AddError(TEXT("Unknown case: ")+Parameters);return false;}
    const bool bDirty=Start==TEXT("Dirty");
    if(Id==TEXT("V1_V04DirtyEventSwap")){V04DirtyEventCase(*this,false,bDirty);return true;}
    if(Id==TEXT("V2_V04DirtyEventDefaultGrassInPlace")){V04DirtyEventCase(*this,true,bDirty);return true;}
    for(const FRollbackCase& C:RollbackCases())if(Id==C.Id){RollbackCase(*this,C,bDirty);return true;}
    AddError(TEXT("Unknown case: ")+Parameters);return false;
}
#endif
