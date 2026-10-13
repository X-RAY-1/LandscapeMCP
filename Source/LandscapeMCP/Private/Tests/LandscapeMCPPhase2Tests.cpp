#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "LandscapeMCPApply.h"
#include "LandscapeMCPCollisionModel.h"
#include "Chaos/HeightField.h"
#include "LandscapeMCPOperations.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeInfo.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "ScopedTransaction.h"
#include "Serialization/ObjectWriter.h"
#include "Tests/AutomationEditorCommon.h"
#include "ToolsetRegistry/ToolsetLibrary.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#include "JsonObjectConverter.h"
#include "Misc/Crc.h"
#include "InstancedFoliageActor.h"
#include "InstancedFoliage.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "Engine/StaticMesh.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "Materials/MaterialExpressionLandscapeGrassOutput.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Settings/EditorLoadingSavingSettings.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"

namespace LandscapeMCP::Phase2Tests
{
    using namespace Authoring;
    struct FFixture
    {
        UWorld* World=nullptr;ALandscape* Actor=nullptr;AActor* Sentinel=nullptr;
        FString Path;int32 Saves=0;FDelegateHandle SaveHandle;
        FFixture()
        {
            ResetAutomationApplyState();World=FAutomationEditorCommonUtils::CreateNewMap();
            if(!World)return;
            const auto C=Create(World->PersistentLevel->GetPathName(),TEXT("V04ApplyFixture"),FVector::ZeroVector,FVector(100),1,1,1,63,0,false);
            if(!C.bSuccess)return;Path=C.LandscapePath;Actor=FindObject<ALandscape>(nullptr,*Path);
            Sentinel=World->SpawnActor<AActor>();
            if(Sentinel)Sentinel->SetActorLabel(TEXT("V04UnrelatedSentinel"));
            SaveHandle=FCoreUObjectDelegates::OnObjectPreSave.AddLambda([this](UObject*,FObjectPreSaveContext){++Saves;});
        }
        ~FFixture()
        {
            FCoreUObjectDelegates::OnObjectPreSave.Remove(SaveHandle);
            SetAutomationApplyFault(EApplyFault::None);
            // Test fixture cleanup only; never a production latch recovery API.
            while(GEditor&&GEditor->Trans&&GEditor->Trans->CanUndo())
            {
                const auto C=GEditor->Trans->GetUndoContext();
                if(!C.Context.StartsWith(TEXT("LandscapeMCP.V04."))&&!C.Context.StartsWith(TEXT("LandscapeMCP.Automation.")))break;
                if(!GEditor->UndoTransaction(false))break;
            }
            if(Actor){Actor->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Heightmap_All);Actor->ForceUpdateLayersContent();}
            ResetAutomationApplyState();
        }
        FPreviewRequest Request(EAuthoringOperation Op)
        {
            FPreviewRequest R;R.Operation=Op;R.LandscapePath=Path;R.bDryRun=false;
            R.Center=FVector2D(3000,3000);R.SizeX=1000;R.SizeY=800;R.Width=400;R.Falloff=300;R.MaxDelta=300;R.TargetHeight=100;
            R.Points={FVector2D(1500,3000),FVector2D(4500,3000)};R.MaxSlope=30;return R;
        }
        void Peak()
        {
            FEditRequest R;R.LandscapePath=Path;R.Center=FVector2D(3000,3000);R.RadiusCm=1;R.Strength=200;R.bDryRun=false;
            Edit(R);
        }
        FHeightfieldSnapshot Read(const FPreviewRequest& R)
        {
            FPath P;FVector2D Min,Max;FString Code,Error;FPreviewRequest D=R;D.bDryRun=true;
            FHeightfieldSnapshot S;if(ValidateRequest(D,P,Min,Max,Code))ReadSnapshot(Path,Min,Max,S,Code,Error);return S;
        }
        void Update(){Actor->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Heightmap_All);Actor->ForceUpdateLayersContent();}
    };
    uint32 ActorHash(AActor* A){TArray<uint8> Bytes;FObjectWriter W(A,Bytes);return FCrc::MemCrc32(Bytes.GetData(),Bytes.Num());}
    bool RunApply(FAutomationTestBase& T,EAuthoringOperation Op,bool Bend=false,FString Mode=TEXT("fixed"),int32 Shape=0)
    {
        FFixture F;if(!T.TestNotNull(TEXT("fixture"),F.Actor))return false;
        if(Shape==0)F.Peak();
        if(Shape!=0)
        {
            TArray<uint16> Input;
            for(int32 Y=0;Y<=63;++Y)for(int32 X=0;X<=63;++X)
                Input.Add(uint16(32768+(Shape==1?0:Shape==2?((X==30&&Y==30)?-256:0):Shape==3?X:-X)));
            const FGuid Layer=F.Actor->GetEditLayer(0)->GetGuid();
            {FScopedSetLandscapeEditingLayer L(F.Actor,Layer);FLandscapeEditDataInterface E(F.Actor->GetLandscapeInfo(),Layer);
            E.SetHeightData(0,0,63,63,Input.GetData(),0,false,nullptr,nullptr,nullptr,false,nullptr,nullptr,true,true,true);E.Flush();}
            F.Update();
        }
        auto R=F.Request(Op);R.HeightMode=Mode;if(Bend)R.Points={FVector2D(1500,2800),FVector2D(3000,3000),FVector2D(4500,3400)};
        auto Original=F.Read(R);FPreviewRequest D=R;D.bDryRun=true;FPreparedTerrainPlan CodecPlan;PlanPreview(D,Original,&CodecPlan);const auto PreviewResult=Execute(D);
        if(!T.TestTrue(*PreviewResult.ErrorCode,PreviewResult.bSuccess))return false;
        const auto SentinelBefore=ActorHash(F.Sentinel);const int32 UndoBefore=UToolsetLibrary::GetActiveUndoCount();
        const auto O=Execute(R);FString Json;FJsonObjectConverter::UStructToJsonObjectString(O,Json);T.AddInfo(TEXT("APPLY ")+Json);
        if(!T.TestTrue(*O.Message,O.bSuccess))return false;
        if(PreviewResult.PlannedModifiedCount==0)
        {
            T.TestEqual(TEXT("flat satisfied"),O.Status,FString(TEXT("NO_CHANGE")));
            T.TestEqual(TEXT("no transaction on satisfied terrain"),UToolsetLibrary::GetActiveUndoCount(),UndoBefore);
            T.TestTrue(TEXT("satisfied source unchanged"),F.Read(R).Heights==Original.Heights);
            T.TestEqual(TEXT("no saves"),F.Saves,0);return true;
        }
        T.TestEqual(TEXT("applied"),O.Status,FString(TEXT("APPLIED")));T.TestTrue(TEXT("actual valid"),O.bActualValid);
        T.TestEqual(TEXT("verified count"),O.ModifiedCount,O.PlannedModifiedCount);
        T.TestEqual(TEXT("one transaction"),UToolsetLibrary::GetActiveUndoCount(),UndoBefore+1);
        T.TestEqual(TEXT("sentinel unchanged"),ActorHash(F.Sentinel),SentinelBefore);
        if(Op==EAuthoringOperation::CreateWalkablePath)
        {T.TestEqual(TEXT("actual walkable ratio"),O.ActualAfter.WalkableRatio,1.0);T.TestEqual(TEXT("no near limit"),O.ActualAfter.NearLimitCount,0);T.TestEqual(TEXT("no unwalkable"),O.ActualAfter.UnwalkableCount,0);}
        const auto Applied=F.Read(R);const auto Owned=GEditor->Trans->GetUndoContext();
        T.TestTrue(TEXT("transaction GUID"),Owned.TransactionId.IsValid());T.TestTrue(TEXT("context"),Owned.Context.StartsWith(TEXT("LandscapeMCP.V04.")));
        const auto Retry=Execute(R);T.TestTrue(*Retry.Message,Retry.bSuccess);T.TestEqual(TEXT("retry no change"),Retry.Status,FString(TEXT("NO_CHANGE")));
        T.TestEqual(TEXT("retry no transaction"),UToolsetLibrary::GetActiveUndoCount(),UndoBefore+1);
        T.TestTrue(TEXT("retry source unchanged"),F.Read(R).Heights==Applied.Heights);
        T.TestTrue(TEXT("human undo"),GEditor->UndoTransaction());F.Update();T.TestTrue(TEXT("source plus halo restored"),F.Read(R).Heights==Original.Heights);
        FString CodecError;const bool UndoCollision=VerifyAutomationCollision(F.Path,F.Read(R),CodecPlan,CodecError);T.TestTrue(*CodecError,UndoCollision);
        T.TestTrue(TEXT("human redo"),GEditor->RedoTransaction());F.Update();T.TestTrue(TEXT("redo planned source"),F.Read(R).Heights==Applied.Heights);
        T.TestEqual(TEXT("redo collision verified through retry"),Execute(R).Status,FString(TEXT("NO_CHANGE")));
        T.TestEqual(TEXT("redo retry no transaction"),UToolsetLibrary::GetActiveUndoCount(),UndoBefore+1);
        // After Undo the cached record is stale for the terrain: the same request
        // is a new Apply from fresh state, not a refusal and not a shortcut.
        T.TestTrue(TEXT("human undo again"),GEditor->UndoTransaction());F.Update();T.TestTrue(TEXT("source restored again"),F.Read(R).Heights==Original.Heights);
        const auto Again=Execute(R);T.TestTrue(*Again.Message,Again.bSuccess);T.TestEqual(TEXT("undo then same request applies again"),Again.Status,FString(TEXT("APPLIED")));
        T.TestTrue(TEXT("reapply actual valid"),Again.bActualValid);T.TestEqual(TEXT("reapply verified count"),Again.ModifiedCount,O.ModifiedCount);
        T.TestEqual(TEXT("reapply one new transaction"),UToolsetLibrary::GetActiveUndoCount(),UndoBefore+1);
        T.TestTrue(TEXT("reapply reproduces the planned source"),F.Read(R).Heights==Applied.Heights);
        T.TestTrue(TEXT("reapply owns a new transaction"),GEditor->Trans->GetUndoContext().TransactionId!=Owned.TransactionId);
        T.TestEqual(TEXT("zero save events"),F.Saves,0);T.TestFalse(TEXT("no active transaction"),GEditor->IsTransactionActive());
        return true;
    }
}
using namespace LandscapeMCP;
using namespace LandscapeMCP::Authoring;
using namespace LandscapeMCP::Phase2Tests;

#define APPLY_TEST(Class,Name,Op,Bend) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class,Name,EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter) \
bool Class::RunTest(const FString&){return RunApply(*this,Op,Bend);}
APPLY_TEST(FV04ApplyRect,"LandscapeMCP.V04.Apply.FlattenRectangle",EAuthoringOperation::FlattenRectangle,false)
APPLY_TEST(FV04ApplyPath,"LandscapeMCP.V04.Apply.FlattenPath",EAuthoringOperation::FlattenPath,false)
APPLY_TEST(FV04ApplyWalk,"LandscapeMCP.V04.Apply.CreateWalkablePath",EAuthoringOperation::CreateWalkablePath,false)
APPLY_TEST(FV04ApplyBend,"LandscapeMCP.V04.Collision.Bend",EAuthoringOperation::FlattenPath,true)
APPLY_TEST(FV04CodecRect,"LandscapeMCP.V04.CollisionCodec.FlattenRectangle",EAuthoringOperation::FlattenRectangle,false)
APPLY_TEST(FV04CodecPath,"LandscapeMCP.V04.CollisionCodec.FlattenPath",EAuthoringOperation::FlattenPath,false)
APPLY_TEST(FV04CodecWalk,"LandscapeMCP.V04.CollisionCodec.CreateWalkablePath",EAuthoringOperation::CreateWalkablePath,false)
APPLY_TEST(FV04CodecUndoRedo,"LandscapeMCP.V04.CollisionCodec.UndoRedo",EAuthoringOperation::FlattenPath,false)
#undef APPLY_TEST

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04Faults,"LandscapeMCP.V04.Rollback.FaultInjectionAndLatch",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04Faults::RunTest(const FString&)
{
    const EApplyFault Faults[]={EApplyFault::SourceMismatch,EApplyFault::FreshReadFailure,EApplyFault::GeometryMismatch,EApplyFault::CollisionFailure,
        EApplyFault::UndoFalse,EApplyFault::RestoreMismatch,EApplyFault::RestoreCollisionFailure,EApplyFault::OwnershipMismatch,EApplyFault::InsertUnrelatedTransaction,EApplyFault::StaleSource};
    for(int32 I=0;I<UE_ARRAY_COUNT(Faults);++I)
    {
        FFixture F;if(!TestNotNull(TEXT("fixture"),F.Actor))return false;auto R=F.Request(EAuthoringOperation::FlattenRectangle);
        const auto Original=F.Read(R);const auto SentinelBefore=ActorHash(F.Sentinel);const auto Prior=GEditor->Trans->GetUndoContext().TransactionId;
        const int32 Before=UToolsetLibrary::GetActiveUndoCount();SetAutomationApplyFault(Faults[I]);const auto O=Execute(R);SetAutomationApplyFault(EApplyFault::None);
        AddInfo(FString::Printf(TEXT("FAULT %c status=%s rollback=%s error=%s latch=%d sourceRestored=%d undoBefore=%d undoAfter=%d latestContext=%s"),TCHAR('A'+I),*O.Status,*O.RollbackStatus,*O.ErrorCode,MutationDisabled(),F.Read(R).Heights==Original.Heights,Before,UToolsetLibrary::GetActiveUndoCount(),*GEditor->Trans->GetUndoContext().Context));
        TestFalse(TEXT("failure is never success"),O.bSuccess);TestFalse(TEXT("actual invalid on failure"),O.bActualValid);TestEqual(TEXT("no partial count"),O.ModifiedCount,0);
        if(I<4)
        {TestEqual(TEXT("verified rollback"),O.Status,FString(TEXT("ROLLED_BACK")));TestEqual(TEXT("restore verified"),O.RollbackStatus,FString(TEXT("VERIFIED")));TestTrue(TEXT("entire original plus halo"),F.Read(R).Heights==Original.Heights);FPreparedTerrainPlan P;auto D=R;D.bDryRun=true;PlanPreview(D,Original,&P);FString E;TestTrue(*E,VerifyAutomationCollision(F.Path,Original,P,E));TestFalse(TEXT("no latch"),MutationDisabled());TestEqual(TEXT("prior transaction protected"),GEditor->Trans->GetUndoContext().TransactionId,Prior);}
        else if(I<9)
        {
            TestEqual(TEXT("rollback failed"),O.Status,FString(TEXT("ROLLBACK_FAILED")));TestTrue(TEXT("latch"),MutationDisabled());
            const auto FailedSource=F.Read(R);const int32 FailedUndo=UToolsetLibrary::GetActiveUndoCount();
            for(EAuthoringOperation Op:{EAuthoringOperation::FlattenRectangle,EAuthoringOperation::FlattenPath,EAuthoringOperation::CreateWalkablePath})
                TestEqual(TEXT("all new mutation rejected"),Execute(F.Request(Op)).ErrorCode,FString(TEXT("MUTATION_DISABLED_AFTER_ROLLBACK_FAILURE")));
            TestTrue(TEXT("latched source unchanged"),F.Read(R).Heights==FailedSource.Heights);TestEqual(TEXT("latched history unchanged"),UToolsetLibrary::GetActiveUndoCount(),FailedUndo);
            auto Dry=R;Dry.bDryRun=true;TestTrue(TEXT("dry run still allowed"),Execute(Dry).bSuccess);
            if(I==8)TestEqual(TEXT("unrelated latest never undone"),GEditor->Trans->GetUndoContext().Context,FString(TEXT("LandscapeMCP.Automation.Unrelated")));
        }
        else
        {TestEqual(TEXT("stale"),O.ErrorCode,FString(TEXT("STALE_PLAN")));TestEqual(TEXT("no transaction"),UToolsetLibrary::GetActiveUndoCount(),Before);TestTrue(TEXT("no write"),F.Read(R).Heights==Original.Heights);}
        TestEqual(TEXT("no collateral"),ActorHash(F.Sentinel),SentinelBefore);TestEqual(TEXT("no save"),F.Saves,0);TestFalse(TEXT("Editor transaction idle"),GEditor->IsTransactionActive());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04Grass,"LandscapeMCP.V04.Safety.GrassGuardAndOuterTransaction",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04Grass::RunTest(const FString&)
{
    FFixture F;if(!TestNotNull(TEXT("fixture"),F.Actor))return false;auto R=F.Request(EAuthoringOperation::FlattenRectangle);const auto Original=F.Read(R);
    F.Actor->LandscapeMaterial=NewObject<UMaterial>(GetTransientPackage());
    TestEqual(TEXT("unknown custom material refused"),Execute(R).ErrorCode,FString(TEXT("UNSUPPORTED_GRASS_STATE")));
    F.Actor->LandscapeMaterial=NewObject<UMaterialInstanceConstant>(GetTransientPackage());
    TestEqual(TEXT("unknown instance refused"),Execute(R).ErrorCode,FString(TEXT("UNSUPPORTED_GRASS_STATE")));
    F.Actor->LandscapeMaterial=nullptr;
    F.Actor->FoliageComponents.Add(NewObject<UHierarchicalInstancedStaticMeshComponent>(F.Actor));
    TestEqual(TEXT("active grass component cache refused"),Execute(R).ErrorCode,FString(TEXT("UNSUPPORTED_GRASS_STATE")));
    F.Actor->FoliageComponents.Reset();
    UMaterial* GrassMaterial=NewObject<UMaterial>(GetTransientPackage());
    GrassMaterial->GetExpressionCollection().AddExpression(NewObject<UMaterialExpressionLandscapeGrassOutput>(GrassMaterial));
    F.Actor->LandscapeMaterial=GrassMaterial;
    TestEqual(TEXT("actual grass output refused"),Execute(R).ErrorCode,FString(TEXT("UNSUPPORTED_GRASS_STATE")));
    F.Actor->LandscapeMaterial=nullptr;
    {
        FScopedTransaction Outer(INVTEXT("Unrelated outer transaction"));F.Sentinel->Modify();
        TestEqual(TEXT("nested apply refused"),Execute(R).ErrorCode,FString(TEXT("ACTIVE_TRANSACTION")));
    }
    auto* SavePolicy=GetMutableDefault<UEditorLoadingSavingSettings>();const bool OldAutoSave=SavePolicy->bAutoSaveEnable;
    SavePolicy->bAutoSaveEnable=true;
    TestEqual(TEXT("autosave unsafe policy refused"),Execute(R).ErrorCode,FString(TEXT("UNSUPPORTED_EDITOR_SAVE_POLICY")));
    SavePolicy->bAutoSaveEnable=OldAutoSave;
    SavePolicy->SetAutomaticallyCheckoutOnAssetModificationOverride(true);
    TestEqual(TEXT("automatic checkout refused"),Execute(R).ErrorCode,FString(TEXT("UNSUPPORTED_EDITOR_SAVE_POLICY")));
    SavePolicy->ResetAutomaticallyCheckoutOnAssetModificationOverride();
    auto& SC=ISourceControlModule::Get();const FName PriorProvider=SC.GetProvider().GetName();
    SC.SetProvider(TEXT("Perforce"));
    TestTrue(TEXT("active provider fixture"),SC.GetProvider().IsEnabled());
    TestEqual(TEXT("active provider refused"),Execute(R).ErrorCode,FString(TEXT("UNSUPPORTED_EDITOR_SAVE_POLICY")));
    SC.SetProvider(PriorProvider);
    TestTrue(TEXT("source unchanged"),F.Read(R).Heights==Original.Heights);TestEqual(TEXT("no save"),F.Saves,0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04Foliage,"LandscapeMCP.V04.Safety.FoliageRegression",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04Foliage::RunTest(const FString&)
{
    FFixture F;if(!TestNotNull(TEXT("fixture"),F.Actor))return false;
    ULandscapeComponent* C=F.Actor->LandscapeComponents[0];
    AInstancedFoliageActor* IFA=AInstancedFoliageActor::GetInstancedFoliageActorForLevel(F.World->PersistentLevel,true);
    UStaticMesh* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    if(!TestNotNull(TEXT("foliage actor"),IFA)||!TestNotNull(TEXT("mesh"),Mesh))return false;
    UFoliageType* Type=nullptr;FFoliageInfo* Info=IFA->AddMesh(Mesh,&Type);
    if(!TestNotNull(TEXT("foliage info"),Info)||!TestNotNull(TEXT("foliage type"),Type))return false;
    FFoliageInstance Instance;Instance.Location=FVector(3000,3000,0);Instance.BaseComponent=C->GetCollisionComponent();
    Info->AddInstance(Type,Instance,C->GetCollisionComponent());
    if(!TestTrue(TEXT("real attached foliage fixture"),AInstancedFoliageActor::HasFoliageAttached(C->GetCollisionComponent())))return false;
    TArray64<uint8> Before,After;UTexture2D* H=C->GetHeightmap(F.Actor->GetEditLayer(0)->GetGuid());H->Source.GetMipData(Before,0);
    const int32 Count=Info->Instances.Num();const auto Location=Info->Instances[0].Location;const auto Rotation=Info->Instances[0].Rotation;const auto Scale=Info->Instances[0].DrawScale3D;
    for(EAuthoringOperation Op:{EAuthoringOperation::FlattenRectangle,EAuthoringOperation::FlattenPath,EAuthoringOperation::CreateWalkablePath})
        TestEqual(TEXT("existing foliage guard retained"),Execute(F.Request(Op)).ErrorCode,FString(TEXT("FOLIAGE_SAFETY_CONFLICT")));
    H->Source.GetMipData(After,0);TestTrue(TEXT("source unchanged"),Before==After);
    TestEqual(TEXT("instance count unchanged"),Info->Instances.Num(),Count);TestEqual(TEXT("instance position unchanged"),Info->Instances[0].Location,Location);
    TestEqual(TEXT("instance rotation unchanged"),Info->Instances[0].Rotation,Rotation);TestTrue(TEXT("instance scale unchanged"),Info->Instances[0].DrawScale3D==Scale);
    TestEqual(TEXT("no save"),F.Saves,0);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04Collateral,"LandscapeMCP.V04.Safety.NoSaveNoCollateral",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04Collateral::RunTest(const FString&)
{
    FFixture F;if(!TestNotNull(TEXT("fixture"),F.Actor))return false;
    const auto Other=Create(F.World->PersistentLevel->GetPathName(),TEXT("UnrelatedLandscape"),FVector(10000,0,0),FVector(100),1,1,1,63,0,false);
    if(!TestTrue(*Other.Message,Other.bSuccess))return false;
    FHeightfieldSnapshot OtherBefore,OtherAfter;FString Code,Error;ReadSnapshot(Other.LandscapePath,FVector2D(10000,0),FVector2D(16300,6300),OtherBefore,Code,Error);
    const auto SentinelBefore=ActorHash(F.Sentinel),SettingsBefore=ActorHash(F.World->GetWorldSettings());
    const int32 ActorCount=F.World->PersistentLevel->Actors.Num();auto R=F.Request(EAuthoringOperation::FlattenRectangle);
    ISourceControlProvider& Provider=ISourceControlModule::Get().GetProvider();int32 SCEvents=0;
    const auto Handle=Provider.RegisterSourceControlStateChanged_Handle(FSourceControlStateChanged::FDelegate::CreateLambda([&](){++SCEvents;}));
    const auto ProviderName=Provider.GetName();const bool ProviderEnabled=Provider.IsEnabled();
    auto O=Execute(R);TestTrue(*O.Message,O.bSuccess);TestTrue(TEXT("undo"),GEditor->UndoTransaction());F.Update();
    ResetAutomationApplyState();SetAutomationApplyFault(EApplyFault::CollisionFailure);O=Execute(R);SetAutomationApplyFault(EApplyFault::None);
    TestEqual(TEXT("verified rollback"),O.Status,FString(TEXT("ROLLED_BACK")));
    TestTrue(TEXT("unrelated fresh read"),ReadSnapshot(Other.LandscapePath,FVector2D(10000,0),FVector2D(16300,6300),OtherAfter,Code,Error));
    TestTrue(TEXT("unrelated full source unchanged"),OtherBefore.Heights==OtherAfter.Heights);
    TestEqual(TEXT("sentinel all serialized properties"),ActorHash(F.Sentinel),SentinelBefore);
    TestEqual(TEXT("world settings all serialized properties"),ActorHash(F.World->GetWorldSettings()),SettingsBefore);
    TestEqual(TEXT("level actor structure"),F.World->PersistentLevel->Actors.Num(),ActorCount);
    TestEqual(TEXT("provider unchanged"),Provider.GetName(),ProviderName);TestEqual(TEXT("provider enabled state unchanged"),Provider.IsEnabled(),ProviderEnabled);
    TestEqual(TEXT("source control state events"),SCEvents,0);Provider.UnregisterSourceControlStateChanged_Handle(Handle);
    TestEqual(TEXT("save events"),F.Saves,0);
    AddInfo(FString::Printf(TEXT("NO_SAVE saveEvents=%d sourceControlEvents=%d provider=%s enabled=%d collateralSourceVertices=%d"),F.Saves,SCEvents,*ProviderName.ToString(),ProviderEnabled,OtherAfter.Heights.Num()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04FlatSlope,"LandscapeMCP.V04.CollisionCodec.SingleSlope",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04FlatSlope::RunTest(const FString&)
{
    FFixture F;if(!TestNotNull(TEXT("fixture"),F.Actor))return false;auto R=F.Request(EAuthoringOperation::FlattenRectangle);
    R.TargetHeight=0;const int32 Before=UToolsetLibrary::GetActiveUndoCount();const auto Flat=Execute(R);
    TestTrue(*Flat.Message,Flat.bSuccess);TestEqual(TEXT("flat no change"),Flat.Status,FString(TEXT("NO_CHANGE")));TestEqual(TEXT("no-change no transaction"),UToolsetLibrary::GetActiveUndoCount(),Before);
    ResetAutomationApplyState();FIntRect Extent;F.Actor->GetLandscapeInfo()->GetLandscapeExtent(Extent);TArray<uint16> Ramp;
    for(int32 Y=0;Y<=63;++Y)for(int32 X=0;X<=63;++X)Ramp.Add(uint16(32768+32*X));
    {
        const FGuid Layer=F.Actor->GetEditLayer(0)->GetGuid();FScopedSetLandscapeEditingLayer L(F.Actor,Layer);FLandscapeEditDataInterface E(F.Actor->GetLandscapeInfo(),Layer);
        E.SetHeightData(0,0,63,63,Ramp.GetData(),0,false,nullptr,nullptr,nullptr,false,nullptr,nullptr,true,true,true);E.Flush();
    }
    F.Update();R.TargetHeight=750;
    const auto Source=F.Read(R);const int32 Cell=44/2,X=Cell%(Source.CountX()-1),Y=Cell/(Source.CountX()-1);
    const double WX=(Source.Rect.Min.X+X+2.0/3)*100,WY=(Source.Rect.Min.Y+Y+1.0/3)*100,Expected=WX/4;
    FHitResult LongHit,ShortHit;FCollisionQueryParams Query(SCENE_QUERY_STAT(V04RampPrecision),true);
    F.World->LineTraceSingleByChannel(LongHit,FVector(WX,WY,Source.Decode(65535)+1000),FVector(WX,WY,Source.Decode(0)-1000),ECC_WorldStatic,Query);
    F.World->LineTraceSingleByChannel(ShortHit,FVector(WX,WY,Expected+2100),FVector(WX,WY,Expected-2100),ECC_WorldStatic,Query);
    AddInfo(FString::Printf(TEXT("COLLISION_PRECISION expected=%.9f longRayHeight=%.9f shortRayHeight=%.9f longError=%.9f shortError=%.9f"),Expected,LongHit.ImpactPoint.Z,ShortHit.ImpactPoint.Z,FMath::Abs(Expected-LongHit.ImpactPoint.Z),FMath::Abs(Expected-ShortHit.ImpactPoint.Z)));
    using namespace CollisionCodec;
    FModel Model;Build(Ramp,64,64,Model);
    const int32 GX=Source.Rect.Min.X+X,GY=Source.Rect.Min.Y+Y,I=GY*64+GX;
    auto Pos=[&](int32 XX,int32 YY,int32 II){return FVector(XX*100,YY*100,Model.Height(II)*100/128.0);};
    const auto Predicted=Triangle(Pos(GX,GY,I),Pos(GX+1,GY,I+1),Pos(GX+1,GY+1,I+65));
    const auto Src=Triangle(FVector(GX*100,GY*100,GX*25),FVector((GX+1)*100,GY*100,(GX+1)*25),FVector((GX+1)*100,(GY+1)*100,(GX+1)*25));
    const double ActualSlope=FMath::RadiansToDegrees(FMath::Acos(ShortHit.ImpactNormal.Z));
    AddInfo(FString::Printf(TEXT("CODEC_SINGLE sourceCentroid=%.12f predictedCentroid=%.12f actualCentroid=%.12f sourceNormal=%s predictedNormal=%s actualNormal=%s sourceSlope=%.12f predictedSlope=%.12f actualSlope=%.12f codecStepCm=%.12f"),Src.Centroid.Z,Predicted.Centroid.Z,ShortHit.ImpactPoint.Z,*Src.Normal.ToString(),*Predicted.Normal.ToString(),*ShortHit.ImpactNormal.ToString(),Src.Slope,Predicted.Slope,ActualSlope,Model.Step*100/128));
    AddInfo(FString::Printf(TEXT("CODEC_SINGLE_NORMAL sourceX=%.15f sourceY=%.15f sourceZ=%.15f predictedX=%.15f predictedY=%.15f predictedZ=%.15f actualX=%.15f actualY=%.15f actualZ=%.15f face=%d"),Src.Normal.X,Src.Normal.Y,Src.Normal.Z,Predicted.Normal.X,Predicted.Normal.Y,Predicted.Normal.Z,ShortHit.ImpactNormal.X,ShortHit.ImpactNormal.Y,ShortHit.ImpactNormal.Z,ShortHit.FaceIndex));
    TestEqual(TEXT("codec centroid actual"),double(ShortHit.ImpactPoint.Z),Predicted.Centroid.Z,1.e-9);
    TestTrue(TEXT("codec normal actual"),FVector::Dist(ShortHit.ImpactNormal,Predicted.Normal)<1.e-12);
    const auto Slope=Execute(R);TestTrue(*Slope.Message,Slope.bSuccess);TestTrue(TEXT("slope actual verified"),Slope.bActualValid);
    TestTrue(TEXT("single slope before nonzero"),Slope.Before.MaxTriangleSlopeDegrees>10);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04ApplyPerformance,"LandscapeMCP.V04.Performance.ApplyAndCacheWorstCases",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool RunCodecPerformance(FAutomationTestBase& T)
{
    FFixture F;if(!T.TestNotNull(TEXT("fixture"),F.Actor))return false;
    const auto Large=Create(F.World->PersistentLevel->GetPathName(),TEXT("MaxReadApply"),FVector(10000,0,0),FVector(80,80,100),3,3,1,63,0,false);
    if(!T.TestTrue(*Large.Message,Large.bSuccess))return false;
    auto R=F.Request(EAuthoringOperation::FlattenRectangle);R.LandscapePath=Large.LandscapePath;R.Center=FVector2D(17480,7480);
    R.SizeX=R.SizeY=5000;R.Falloff=2500;R.TargetHeight=10;
    const double Start=FPlatformTime::Seconds();const auto O=Execute(R);
    T.TestTrue(*O.Message,O.bSuccess);T.TestEqual(TEXT("maximum read vertices"),O.ReadSampleCount,16384);
    T.AddInfo(FString::Printf(TEXT("PERFORMANCE_PHASE2 maxReadApply vertices=%d affected=%d triangles=%d totalMs=%.3f"),O.ReadSampleCount,O.AffectedSampleCount,O.ActualAfter.TriangleCount,(FPlatformTime::Seconds()-Start)*1000));
    // Native, legal Apply above. The cache fixture below is read-only and restores
    // source pointers before returning to the Editor tick; it never writes terrain.
    for(int32 I=0;I<32;++I)
    {
        const auto C=Create(F.World->PersistentLevel->GetPathName(),FString::Printf(TEXT("ScanLandscape%d"),I),FVector(30000+I*1000,0,0),FVector(100),1,1,1,7,0,false);
        if(!T.TestTrue(*C.Message,C.bSuccess))return false;
    }
    R.TargetHeight=20;const double CombinedStart=FPlatformTime::Seconds();const auto Combined=Execute(R);
    T.TestTrue(*Combined.Message,Combined.bSuccess);
    T.AddInfo(FString::Printf(TEXT("PERFORMANCE_RESUME largeApplyManyActors totalMs=%.3f vertices=%d"),(FPlatformTime::Seconds()-CombinedStart)*1000,Combined.ReadSampleCount));
    const auto Cache=Create(F.World->PersistentLevel->GetPathName(),TEXT("CacheBudgetFixture"),FVector(100000,0,0),FVector(100),4,2,1,7,0,false);
    if(!T.TestTrue(*Cache.Message,Cache.bSuccess))return false;
    ALandscape* A=FindObject<ALandscape>(nullptr,*Cache.LandscapePath);const FGuid Layer=A->GetEditLayer(0)->GetGuid();TArray<UTexture2D*> Originals;
    TArray<FColor> Pixels;Pixels.Init(FColor(128,0,128,128),1024*1024);
    for(ULandscapeComponent* C:A->LandscapeComponents)
    {
        Originals.Add(C->GetHeightmap(Layer));UTexture2D* CacheTexture=NewObject<UTexture2D>(A->GetLevel()->GetOutermost());
        CacheTexture->Source.Init(1024,1024,1,1,TSF_BGRA8,reinterpret_cast<const uint8*>(Pixels.GetData()));C->GetLayerData(Layer)->HeightmapData.Texture=CacheTexture;
    }
    FHeightfieldSnapshot S;FString Code,Error;const double ReadStart=FPlatformTime::Seconds();
    const bool ReadOK=ReadSnapshot(Cache.LandscapePath,FVector2D(100000,0),FVector2D(102800,1400),S,Code,Error);
    const double ReadMs=(FPlatformTime::Seconds()-ReadStart)*1000;
    for(int32 I=0;I<A->LandscapeComponents.Num();++I)A->LandscapeComponents[I]->GetLayerData(Layer)->HeightmapData.Texture=Originals[I];
    T.TestTrue(*Error,ReadOK);T.TestEqual(TEXT("exact 32MiB cache exercised"),S.CachedSourceBytes,int64(32*1024*1024));
    T.AddInfo(FString::Printf(TEXT("PERFORMANCE_PHASE2 readonlyCache bytes=%lld vertices=%d extraLandscapeActors=34 elapsedMs=%.3f"),S.CachedSourceBytes,S.Heights.Num(),ReadMs));
    T.TestEqual(TEXT("no save"),F.Saves,0);return true;
}

bool FV04ApplyPerformance::RunTest(const FString&){return RunCodecPerformance(*this);}

namespace LandscapeMCP::Phase2Tests
{
    class FTransportHarnessCommand final:public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;
        FString Directory;
        TUniquePtr<FFixture> Fixture;
        FHeightfieldSnapshot Applied;
        int32 UndoBefore=0;
        int32 Stage=0;
        double Deadline=0;
        void Ready()
        {
            FString Text=FString::Printf(TEXT("{\"stage\":%d,\"landscapePath\":\"%s\"}"),Stage,*Fixture->Path);
            FFileHelper::SaveStringToFile(Text,*(Directory/FString::Printf(TEXT("ready%d.json"),Stage)));
            Deadline=FPlatformTime::Seconds()+120;
        }
    public:
        FTransportHarnessCommand(FAutomationTestBase* T,FString D):Test(T),Directory(MoveTemp(D)){}
        virtual bool Update()override
        {
            if(!Fixture)
            {
                Fixture=MakeUnique<FFixture>();if(!Fixture->Actor){Test->AddError(TEXT("MCP harness fixture creation failed."));return true;}
                UndoBefore=UToolsetLibrary::GetActiveUndoCount();IFileManager::Get().MakeDirectory(*Directory,true);Ready();return false;
            }
            if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("MCP transport harness timed out waiting for external HTTP client."));Fixture.Reset();return true;}
            if(!IFileManager::Get().FileExists(*(Directory/FString::Printf(TEXT("done%d"),Stage))))return false;
            auto R=Fixture->Request(EAuthoringOperation::FlattenRectangle);
            Test->TestEqual(TEXT("MCP no save"),Fixture->Saves,0);
            if(Stage==0)
            {
                Test->TestEqual(TEXT("HTTP apply and retry create exactly one transaction"),UToolsetLibrary::GetActiveUndoCount(),UndoBefore+1);
                Applied=Fixture->Read(R);Test->TestTrue(TEXT("HTTP apply modified fixture"),Applied.Heights.Contains(32896));
                SetAutomationApplyFault(EApplyFault::CollisionFailure);++Stage;Ready();return false;
            }
            if(Stage==1)
            {
                Test->TestFalse(TEXT("verified HTTP rollback does not latch"),MutationDisabled());
                Test->TestTrue(TEXT("HTTP rollback fresh source restored"),Fixture->Read(R).Heights==Applied.Heights);
                SetAutomationApplyFault(EApplyFault::UndoFalse);++Stage;Ready();return false;
            }
            SetAutomationApplyFault(EApplyFault::None);Test->TestTrue(TEXT("HTTP failed restoration latches"),MutationDisabled());
            const auto Latest=GEditor->Trans->GetUndoContext();
            if(Latest.Context.StartsWith(TEXT("LandscapeMCP.V04.")))Test->TestTrue(TEXT("test-only restore after simulated Undo failure"),GEditor->UndoTransaction(false));
            Fixture->Update();Test->TestTrue(TEXT("isolated Editor usable after test recovery"),Fixture->Read(R).Heights==Applied.Heights);
            Test->AddInfo(TEXT("MCP_TRANSPORT HTTP DryRun/Apply/retry/error/verified rollback/failed rollback/latch exercised; external response assertions in transport evidence."));
            Fixture.Reset();FFileHelper::SaveStringToFile(TEXT("complete"),*(Directory/TEXT("complete")));return true;
        }
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04Transport,"LandscapeMCP.V04.MCP.TransportRoundTrip",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04Transport::RunTest(const FString&)
{
    FString Directory;
    if(!FParse::Value(FCommandLine::Get(),TEXT("LandscapeMCPTransportHarness="),Directory))
    {AddWarning(TEXT("Transport harness not requested. Run with -LandscapeMCPTransportHarness=<directory> and the external HTTP client; this run does not prove MCP E2E."));return true;}
    AddExpectedMessage(TEXT("UNSUPPORTED_RECTANGLE_YAW"),ELogVerbosity::Warning,EAutomationExpectedMessageFlags::Contains,1,false);
    AddExpectedMessage(TEXT("COLLISION_SYNC_FAILED"),ELogVerbosity::Warning,EAutomationExpectedMessageFlags::Contains,2,false);
    AddExpectedMessage(TEXT("MUTATION_DISABLED_AFTER_ROLLBACK_FAILURE"),ELogVerbosity::Warning,EAutomationExpectedMessageFlags::Contains,1,false);
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FTransportHarnessCommand>(this,Directory));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04ProductionSmoke,"LandscapeMCP.V04.Resume.ProductionPathSmoke",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04ProductionSmoke::RunTest(const FString&)
{
    for(EAuthoringOperation Op:{EAuthoringOperation::FlattenRectangle,EAuthoringOperation::FlattenPath,EAuthoringOperation::CreateWalkablePath})
        if(!RunApply(*this,Op,false,TEXT("fixed"),0))return false;
    for(const FString Mode:{FString(TEXT("follow_start")),FString(TEXT("follow_path"))})
        if(!RunApply(*this,EAuthoringOperation::FlattenPath,true,Mode,3))return false;
    AddInfo(TEXT("PRODUCTION_SMOKE Execute has a single production path (no draft scope exists); all3 DryRun/Apply/Retry/Undo/Redo; walkability and codec collision verified."));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04CodecFaults,"LandscapeMCP.V04.CollisionCodec.FaultInjection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04CodecFaults::RunTest(const FString&)
{
    for(const auto Fault:{EApplyFault::CollisionFailure,EApplyFault::RestoreCollisionFailure})
    {
        FFixture F;if(!TestNotNull(TEXT("fixture"),F.Actor))return false;
        auto R=F.Request(EAuthoringOperation::FlattenRectangle);const auto Before=F.Read(R);
        SetAutomationApplyFault(Fault);const auto O=Execute(R);SetAutomationApplyFault(EApplyFault::None);
        const bool D=Fault==EApplyFault::CollisionFailure;
        AddInfo(FString::Printf(TEXT("CODEC_FAULT fault=%s status=%s rollback=%s sourceRestored=%d latch=%d message=%s"),D?TEXT("D"):TEXT("G"),*O.Status,*O.RollbackStatus,F.Read(R).Heights==Before.Heights,MutationDisabled(),*O.Message));
        TestEqual(TEXT("targeted outcome"),O.Status,FString(D?TEXT("ROLLED_BACK"):TEXT("ROLLBACK_FAILED")));
        TestTrue(TEXT("source exact restored"),F.Read(R).Heights==Before.Heights);TestEqual(TEXT("latch"),MutationDisabled(),!D);
        TestEqual(TEXT("no saves"),F.Saves,0);
    }
    return true;
}
namespace LandscapeMCP::Phase2Tests
{
    FHeightfieldSnapshot FullSource(FFixture& F)
    {
        FHeightfieldSnapshot S;FString Code,Error;
        ReadSnapshot(F.Path,FVector2D(0,0),FVector2D(6300,6300),S,Code,Error);return S;
    }
    void WriteRamp(FFixture& F)
    {
        TArray<uint16> Ramp;for(int32 Y=0;Y<=63;++Y)for(int32 X=0;X<=63;++X)Ramp.Add(uint16(32768+X));
        const FGuid Layer=F.Actor->GetEditLayer(0)->GetGuid();
        {FScopedSetLandscapeEditingLayer L(F.Actor,Layer);FLandscapeEditDataInterface E(F.Actor->GetLandscapeInfo(),Layer);
        E.SetHeightData(0,0,63,63,Ramp.GetData(),0,false,nullptr,nullptr,nullptr,false,nullptr,nullptr,true,true,true);E.Flush();}
        F.Update();
    }
    bool FullCollision(FAutomationTestBase& T,FFixture& F,const FHeightfieldSnapshot& S)
    {
        FPreparedTerrainPlan P;P.Core.Init(1,2*(S.CountX()-1)*(S.CountY()-1));P.Transition.Init(0,P.Core.Num());FString Error;
        const bool OK=VerifyAutomationCollision(F.Path,S,P,Error);return T.TestTrue(*Error,OK);
    }
    bool RangeEditor(FAutomationTestBase& T,bool Rollback)
    {
        using namespace CollisionCodec;FFixture F;if(!T.TestNotNull(TEXT("fixture"),F.Actor))return false;
        WriteRamp(F);const auto Before=FullSource(F);FModel B;Build(Before.Heights,64,64,B);
        if(!FullCollision(T,F,Before))return false;
        auto R=F.Request(EAuthoringOperation::FlattenRectangle);R.TargetHeight=100;
        if(Rollback)SetAutomationApplyFault(EApplyFault::CollisionFailure);
        const auto O=Execute(R);SetAutomationApplyFault(EApplyFault::None);
        if(Rollback)
        {
            T.TestEqual(*O.Message,O.Status,FString(TEXT("ROLLED_BACK")));T.TestFalse(TEXT("verified no latch"),MutationDisabled());
            const auto Restored=FullSource(F);T.TestTrue(TEXT("range rollback exact source"),Restored.Heights==Before.Heights);
            FullCollision(T,F,Restored);T.AddInfo(TEXT("CODEC_ROLLBACK expanded source range then original source+original collision restored"));
        }
        else
        {
            if(!T.TestTrue(*O.Message,O.bSuccess))return false;
            const auto After=FullSource(F);FModel A;Build(After.Heights,64,64,A);
            const int32 Untouched=10*64+10;T.TestEqual(TEXT("unchanged source"),Before.Heights[Untouched],After.Heights[Untouched]);
            T.TestTrue(TEXT("range expands"),A.Range>B.Range);T.TestTrue(TEXT("unchanged collision requantized"),A.Height(Untouched)!=B.Height(Untouched));
            FullCollision(T,F,After);
            T.AddInfo(FString::Printf(TEXT("CODEC_RANGE_EDITOR unchangedRaw=%u beforeCm=%.12f afterCm=%.12f beforeStepCm=%.12f afterStepCm=%.12f beforeRange=%.12f afterRange=%.12f"),After.Heights[Untouched],B.Height(Untouched)*100/128,A.Height(Untouched)*100/128,B.Step*100/128,A.Step*100/128,B.Range,A.Range));
            T.TestTrue(TEXT("human undo expansion"),GEditor->UndoTransaction());F.Update();const auto Undo=FullSource(F);
            T.TestTrue(TEXT("undo exact source"),Undo.Heights==Before.Heights);FullCollision(T,F,Undo);
            auto* C=F.Actor->LandscapeComponents[0]->GetCollisionComponent();
            T.TestEqual(TEXT("complex full-rebuild contracts after undo"),C->HeightfieldRef->HeightfieldGeometry->GeomData.Range,B.Range);
            T.TestTrue(TEXT("redo expansion"),GEditor->RedoTransaction());F.Update();T.TestTrue(TEXT("redo source"),FullSource(F).Heights==After.Heights);FullCollision(T,F,FullSource(F));
        }
        T.TestEqual(TEXT("no saves"),F.Saves,0);return true;
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04CodecRangeEditor,"LandscapeMCP.V04.CollisionCodec.RangeExpansionEditor",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04CodecRangeEditor::RunTest(const FString&){return RangeEditor(*this,false);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04CodecRollback,"LandscapeMCP.V04.CollisionCodec.Rollback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04CodecRollback::RunTest(const FString&){return RangeEditor(*this,true);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04CodecPerformance,"LandscapeMCP.V04.CollisionCodec.Performance",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04CodecPerformance::RunTest(const FString&){return RunCodecPerformance(*this);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04CodecRestoreFault,"LandscapeMCP.V04.CollisionCodec.RollbackActualPhysicsMismatch",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04CodecRestoreFault::RunTest(const FString&)
{
    for(const auto Fault:{EApplyFault::RestoreCodecMismatch,EApplyFault::RestoreCollisionStale})
    {
        FFixture F;if(!TestNotNull(TEXT("fixture"),F.Actor))return false;WriteRamp(F);const auto Before=FullSource(F);
        auto R=F.Request(EAuthoringOperation::FlattenRectangle);SetAutomationApplyFault(Fault);const auto O=Execute(R);SetAutomationApplyFault(EApplyFault::None);
        TestEqual(*O.Message,O.Status,FString(TEXT("ROLLBACK_FAILED")));TestTrue(TEXT("latch"),MutationDisabled());
        TestTrue(TEXT("source exact despite actual collision mismatch"),FullSource(F).Heights==Before.Heights);
        AddInfo(FString::Printf(TEXT("CODEC_ACTUAL_RESTORE_FAULT kind=%s sourceExact=1 status=%s latch=%d message=%s"),Fault==EApplyFault::RestoreCodecMismatch?TEXT("codecMismatch"):TEXT("stalePhysics"),*O.Status,MutationDisabled(),*O.Message));
        TestEqual(TEXT("latched request"),Execute(R).ErrorCode,FString(TEXT("MUTATION_DISABLED_AFTER_ROLLBACK_FAILURE")));
        TestEqual(TEXT("no saves"),F.Saves,0);
    }
    return true;
}
namespace LandscapeMCP::Phase2Tests
{
    bool WorldOffset(FAutomationTestBase& T,double Z)
    {
        FFixture F;if(!T.TestNotNull(TEXT("fixture"),F.Actor))return false;
        const auto Created=Create(F.World->PersistentLevel->GetPathName(),TEXT("WorldZCodec"),FVector(10000,0,Z),FVector(100),1,1,1,63,Z,false);
        if(!T.TestTrue(*Created.Message,Created.bSuccess))return false;
        F.Path=Created.LandscapePath;F.Actor=FindObject<ALandscape>(nullptr,*F.Path);
        auto R=F.Request(EAuthoringOperation::FlattenRectangle);R.Center.X+=10000;R.TargetHeight=Z+100;
        const auto Before=F.Read(R);FPreparedTerrainPlan P;auto Dry=R;Dry.bDryRun=true;PlanPreview(Dry,Before,&P);
        const auto O=Execute(R);if(!T.TestTrue(*O.Message,O.bSuccess))return false;
        FString Error;T.TestTrue(TEXT("world offset actual verify"),VerifyAutomationCollision(F.Path,F.Read(R),P,Error));
        T.TestTrue(TEXT("world offset undo"),GEditor->UndoTransaction());F.Update();T.TestTrue(TEXT("world offset exact source"),F.Read(R).Heights==Before.Heights);
        T.TestTrue(TEXT("world offset undo collision"),VerifyAutomationCollision(F.Path,F.Read(R),P,Error));
        T.TestEqual(TEXT("world Z before"),O.Before.MinHeightCm,Z);
        T.AddInfo(FString::Printf(TEXT("CODEC_WORLD_OFFSET worldZ=%.12f actualMin=%.12f actualMax=%.12f sourceExactAfterUndo=1 collisionVerified=1"),Z,O.ActualAfter.MinHeightCm,O.ActualAfter.MaxHeightCm));
        T.TestEqual(TEXT("no saves"),F.Saves,0);return true;
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04CodecNegativeWorld,"LandscapeMCP.V04.CollisionCodec.NegativeWorldHeight",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04CodecNegativeWorld::RunTest(const FString&){return WorldOffset(*this,-1000000);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04CodecLargeWorld,"LandscapeMCP.V04.CollisionCodec.LargeWorldHeight",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04CodecLargeWorld::RunTest(const FString&){return WorldOffset(*this,1000000);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04CodecBoundarySource,"LandscapeMCP.V04.CollisionCodec.ComponentBoundarySource",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04CodecBoundarySource::RunTest(const FString&)
{
    FFixture F;if(!TestNotNull(TEXT("fixture"),F.Actor))return false;
    const auto Created=Create(F.World->PersistentLevel->GetPathName(),TEXT("BoundaryCodec"),FVector(10000,0,0),FVector(100),2,1,1,63,0,false);
    if(!TestTrue(*Created.Message,Created.bSuccess))return false;
    F.Path=Created.LandscapePath;F.Actor=FindObject<ALandscape>(nullptr,*F.Path);
    FHeightfieldSnapshot S;FString Code,Error;
    if(!TestTrue(TEXT("full source"),ReadSnapshot(F.Path,FVector2D(10000,0),FVector2D(22600,6300),S,Code,Error)))return false;
    FPreparedTerrainPlan P;P.Core.Init(1,2*(S.CountX()-1)*(S.CountY()-1));P.Transition.Init(0,P.Core.Num());
    ULandscapeComponent* C=nullptr;for(ULandscapeComponent* Candidate:F.Actor->LandscapeComponents)if(Candidate->GetSectionBase()==FIntPoint(63,0))C=Candidate;
    if(!TestNotNull(TEXT("right component"),C))return false;
    const FGuid Layer=F.Actor->GetEditLayer(0)->GetGuid();auto* Texture=C->GetHeightmap(Layer);
    const int32 TX=int32(C->HeightmapScaleBias.Z*Texture->Source.GetSizeX());
    const int32 TY=int32(C->HeightmapScaleBias.W*Texture->Source.GetSizeY())+30;
    const int32 Index=TY*Texture->Source.GetSizeX()+TX;
    FColor* Pixels=reinterpret_cast<FColor*>(Texture->Source.LockMip(0));const FColor Saved=Pixels[Index];Pixels[Index].G^=1;Texture->Source.UnlockMip(0);
    FHeightfieldSnapshot Ambiguous;FString ReadError;const bool GlobalRead=ReadSnapshot(F.Path,FVector2D(10000,0),FVector2D(22600,6300),Ambiguous,Code,ReadError);
    const bool Accepted=VerifyAutomationCollision(F.Path,S,P,Error);
    Pixels=reinterpret_cast<FColor*>(Texture->Source.LockMip(0));Pixels[Index]=Saved;Texture->Source.UnlockMip(0);
    TestTrue(TEXT("legacy global source misses second duplicate edge"),GlobalRead&&Ambiguous.Heights==S.Heights);
    TestFalse(TEXT("component-specific duplicate border corruption rejected"),Accepted);
    TestTrue(TEXT("rejected independently of actual flat trace"),Error.Contains(TEXT("Codec source domain does not match")));
    AddInfo(TEXT("CODEC_COMPONENT_BOUNDARY duplicate-edge disagreement rejected before trace; component-local source restored."));
    Error.Reset();TestTrue(TEXT("restored complete component sources and collision"),VerifyAutomationCollision(F.Path,S,P,Error));
    TestEqual(TEXT("no saves"),F.Saves,0);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04ResumeModes,"LandscapeMCP.V04.Resume.PathModesUndoRedo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04ResumeModes::RunTest(const FString&)
{
    for(const FString Mode:{FString(TEXT("fixed")),FString(TEXT("follow_start")),FString(TEXT("follow_path"))})
        if(!RunApply(*this,EAuthoringOperation::FlattenPath,true,Mode,3))return false;
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04ResumeShapes,"LandscapeMCP.V04.Resume.Shapes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04ResumeShapes::RunTest(const FString&)
{
    for(int32 Shape=1;Shape<=4;++Shape)
        for(EAuthoringOperation Op:{EAuthoringOperation::FlattenRectangle,EAuthoringOperation::FlattenPath,EAuthoringOperation::CreateWalkablePath})
            if(!RunApply(*this,Op,true,TEXT("fixed"),Shape))return false;
    return true;
}
// Release amendment: WorldStatic props on the verification ray must not replace
// the target Landscape collision component as the object being verified.
namespace LandscapeMCP::Phase2Tests
{
    AStaticMeshActor* SpawnProp(FFixture& F,FVector Location,FVector Scale)
    {
        UStaticMesh* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
        AStaticMeshActor* A=Mesh?F.World->SpawnActor<AStaticMeshActor>(Location,FRotator::ZeroRotator):nullptr;
        if(!A)return nullptr;
        A->GetStaticMeshComponent()->SetStaticMesh(Mesh);A->SetActorScale3D(Scale);return A;
    }
    AActor* FirstWorldStaticHit(FFixture& F,double X,double Y,double* OutZ=nullptr)
    {
        FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(V04PropFixture),true);
        const bool bHit=F.World->LineTraceSingleByChannel(Hit,FVector(X,Y,2000),FVector(X,Y,-2000),ECC_WorldStatic,Query);
        if(OutZ)*OutZ=bHit?Hit.ImpactPoint.Z:0;return bHit?Hit.GetActor():nullptr;
    }
    struct FPropCase
    {
        const TCHAR* Name;EAuthoringOperation Op;bool bFirstPropAbove;
        TArray<TPair<FVector,FVector>> Props;
    };
    bool RunPropCase(FAutomationTestBase& T,const FPropCase& Case)
    {
        FFixture F;if(!T.TestNotNull(TEXT("fixture"),F.Actor))return false;
        TArray<AStaticMeshActor*> Props;TArray<FTransform> Before;
        for(const auto& P:Case.Props)
        {
            AStaticMeshActor* A=SpawnProp(F,P.Key,P.Value);if(!T.TestNotNull(TEXT("prop"),A))return false;
            Props.Add(A);Before.Add(A->GetActorTransform());
        }
        // Probe at a native triangle centroid column, exactly where the verifier traces.
        double ProbeZ=0;AActor* First=FirstWorldStaticHit(F,3033.0+1.0/3,3000+200.0/3,&ProbeZ);
        T.AddInfo(FString::Printf(TEXT("PROP_FIXTURE case=%s firstWorldStaticHit=%s z=%.3f"),Case.Name,*GetNameSafe(First),ProbeZ));
        if(Case.bFirstPropAbove)T.TestTrue(TEXT("fixture: a prop is the first WorldStatic hit above the target"),Props.Contains(First));
        else T.TestTrue(TEXT("fixture: Landscape is the first WorldStatic hit"),First==F.Actor);
        auto R=F.Request(Case.Op);const auto Original=F.Read(R);FPreparedTerrainPlan Plan;auto D=R;D.bDryRun=true;PlanPreview(D,Original,&Plan);
        FString BeforeError;const bool BeforeOK=VerifyAutomationCollision(F.Path,Original,Plan,BeforeError);
        const int32 Undo=UToolsetLibrary::GetActiveUndoCount();const double Start=FPlatformTime::Seconds();
        const auto O=Execute(R);const double Ms=(FPlatformTime::Seconds()-Start)*1000;
        T.AddInfo(FString::Printf(TEXT("PROP_OBSTRUCTION case=%s props=%d beforeCollision=%d beforeError=%s status=%s error=%s applyMs=%.3f message=%s"),
            Case.Name,Props.Num(),BeforeOK,*BeforeError.Left(160),*O.Status,*O.ErrorCode,Ms,*O.Message.Left(240)));
        T.TestTrue(*FString::Printf(TEXT("%s: original target collision verified"),Case.Name),BeforeOK);
        T.TestEqual(*FString::Printf(TEXT("%s: applied"),Case.Name),O.Status,FString(TEXT("APPLIED")));
        if(O.Status==TEXT("APPLIED"))
        {
            T.TestEqual(TEXT("one transaction"),UToolsetLibrary::GetActiveUndoCount(),Undo+1);
            FString Error;T.TestTrue(TEXT("applied target collision verified with props"),VerifyAutomationCollision(F.Path,F.Read(R),Plan,Error));
            T.TestTrue(TEXT("human undo"),GEditor->UndoTransaction());F.Update();
            T.TestTrue(TEXT("undo exact source"),F.Read(R).Heights==Original.Heights);
            T.TestTrue(TEXT("undo target collision verified with props"),VerifyAutomationCollision(F.Path,F.Read(R),Plan,Error));
        }
        else T.TestEqual(TEXT("refusal wrote nothing"),UToolsetLibrary::GetActiveUndoCount(),Undo);
        for(int32 I=0;I<Props.Num();++I)T.TestTrue(TEXT("prop untouched"),Props[I]->GetActorTransform().Equals(Before[I],0));
        T.TestEqual(TEXT("no saves"),F.Saves,0);return true;
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04PropObstruction,"LandscapeMCP.V04.Collision.PropObstruction",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04PropObstruction::RunTest(const FString&)
{
    const FPropCase Cases[]={
        {TEXT("A_LandscapeOnly"),EAuthoringOperation::FlattenRectangle,false,{}},
        {TEXT("B_CubeAbove"),EAuthoringOperation::FlattenRectangle,true,{{FVector(3000,3000,600),FVector(2)}}},
        {TEXT("C_CubeBelow"),EAuthoringOperation::FlattenRectangle,false,{{FVector(3000,3000,-600),FVector(2)}}},
        {TEXT("D_WallAbovePath"),EAuthoringOperation::FlattenPath,true,{{FVector(3050,3000,500),FVector(0.8,12,4)}}},
        {TEXT("E_MultipleProps"),EAuthoringOperation::FlattenRectangle,true,{{FVector(3000,3000,600),FVector(2)},{FVector(2600,2800,900),FVector(3,1,1)},
            {FVector(3400,3300,400),FVector(1)},{FVector(3000,3000,1200),FVector(6,6,0.2)},{FVector(2800,3200,-500),FVector(2)},{FVector(3300,2700,-900),FVector(4)}}}};
    for(const FPropCase& Case:Cases)if(!RunPropCase(*this,Case))return false;
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04PropRollback,"LandscapeMCP.V04.Collision.PropObstructionRollback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04PropRollback::RunTest(const FString&)
{
    // Verified rollback must still be provable under a prop. Stale/corrupt
    // ACTUAL target physics must still fail restoration under the same prop.
    for(const auto Fault:{EApplyFault::CollisionFailure,EApplyFault::RestoreCodecMismatch,EApplyFault::RestoreCollisionStale})
    {
        FFixture F;if(!TestNotNull(TEXT("fixture"),F.Actor))return false;WriteRamp(F);const auto Before=FullSource(F);
        AStaticMeshActor* Prop=SpawnProp(F,FVector(3000,3000,900),FVector(6,6,1));if(!TestNotNull(TEXT("prop"),Prop))return false;
        TestTrue(TEXT("fixture: prop is the first WorldStatic hit above the target"),FirstWorldStaticHit(F,3000,3000)==Prop);
        auto R=F.Request(EAuthoringOperation::FlattenRectangle);
        SetAutomationApplyFault(Fault);const auto O=Execute(R);SetAutomationApplyFault(EApplyFault::None);
        const bool Verified=Fault==EApplyFault::CollisionFailure;
        AddInfo(FString::Printf(TEXT("PROP_ROLLBACK fault=%d status=%s rollback=%s error=%s latch=%d sourceExact=%d message=%s"),int32(Fault),*O.Status,*O.RollbackStatus,*O.ErrorCode,MutationDisabled(),FullSource(F).Heights==Before.Heights,*O.Message.Left(240)));
        TestEqual(TEXT("outcome under prop"),O.Status,FString(Verified?TEXT("ROLLED_BACK"):TEXT("ROLLBACK_FAILED")));
        TestEqual(TEXT("rollback status under prop"),O.RollbackStatus,FString(Verified?TEXT("VERIFIED"):TEXT("FAILED")));
        TestEqual(TEXT("latch"),MutationDisabled(),!Verified);
        TestTrue(TEXT("source exact"),FullSource(F).Heights==Before.Heights);
        if(Verified)FullCollision(*this,F,FullSource(F));
        TestEqual(TEXT("no saves"),F.Saves,0);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04OverlappingLandscape,"LandscapeMCP.V04.Collision.OverlappingLandscape",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04OverlappingLandscape::RunTest(const FString&)
{
    // An unrelated Landscape directly above must be neither modified nor
    // mistaken for the target: its surface is 300cm or more from every expectation.
    FFixture F;if(!TestNotNull(TEXT("fixture"),F.Actor))return false;
    const auto Other=Create(F.World->PersistentLevel->GetPathName(),TEXT("OverheadLandscape"),FVector(0,0,400),FVector(100),1,1,1,63,400,false);
    if(!TestTrue(*Other.Message,Other.bSuccess))return false;
    TestTrue(TEXT("fixture: unrelated Landscape is the first WorldStatic hit"),FirstWorldStaticHit(F,3000,3000)==FindObject<ALandscape>(nullptr,*Other.LandscapePath));
    FHeightfieldSnapshot OtherBefore,OtherAfter;FString Code,Error;ReadSnapshot(Other.LandscapePath,FVector2D(0,0),FVector2D(6300,6300),OtherBefore,Code,Error);
    auto R=F.Request(EAuthoringOperation::FlattenRectangle);
    const auto O=Execute(R);
    AddInfo(FString::Printf(TEXT("OVERLAPPING_LANDSCAPE status=%s error=%s message=%s"),*O.Status,*O.ErrorCode,*O.Message.Left(240)));
    TestEqual(TEXT("target applied under unrelated Landscape"),O.Status,FString(TEXT("APPLIED")));
    TestTrue(TEXT("unrelated read"),ReadSnapshot(Other.LandscapePath,FVector2D(0,0),FVector2D(6300,6300),OtherAfter,Code,Error));
    TestTrue(TEXT("unrelated Landscape source unchanged"),OtherBefore.Heights==OtherAfter.Heights);
    // The overhead Landscape's own collision is still attributed to itself.
    FPreparedTerrainPlan Full;Full.Core.Init(1,2*(OtherAfter.CountX()-1)*(OtherAfter.CountY()-1));Full.Transition.Init(0,Full.Core.Num());
    Error.Reset();const bool OtherOK=VerifyAutomationCollision(Other.LandscapePath,OtherAfter,Full,Error);TestTrue(*Error,OtherOK);
    TestEqual(TEXT("no saves"),F.Saves,0);return true;
}
// Release amendment: retry history is a bounded idempotency cache, never a deny-list.
namespace LandscapeMCP::Phase2Tests
{
    double Timed(TFunctionRef<void()> Body){const double Start=FPlatformTime::Seconds();Body();return (FPlatformTime::Seconds()-Start)*1000;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04RetryIdempotency,"LandscapeMCP.V04.Retry.Idempotency",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04RetryIdempotency::RunTest(const FString&)
{
    FFixture F;if(!TestNotNull(TEXT("fixture"),F.Actor))return false;
    auto A=F.Request(EAuthoringOperation::FlattenRectangle);
    auto B=F.Request(EAuthoringOperation::FlattenRectangle);B.Center=FVector2D(3400,3200);B.TargetHeight=40;
    const auto Original=F.Read(A);const int32 Undo0=UToolsetLibrary::GetActiveUndoCount();
    FLandscapeMCPTerrainOperationResult O;
    // Immediate duplicate.
    const double ApplyMs=Timed([&]{O=Execute(A);});
    if(!TestEqual(*O.Message,O.Status,FString(TEXT("APPLIED"))))return false;
    const auto AfterA=F.Read(A);const FGuid FirstTransaction=GEditor->Trans->GetUndoContext().TransactionId;
    const double DuplicateMs=Timed([&]{O=Execute(A);});
    TestEqual(TEXT("duplicate NO_CHANGE"),O.Status,FString(TEXT("NO_CHANGE")));TestTrue(TEXT("duplicate success"),O.bSuccess);
    TestEqual(TEXT("duplicate modified 0"),O.ModifiedCount,0);TestEqual(TEXT("duplicate transaction +0"),UToolsetLibrary::GetActiveUndoCount(),Undo0+1);
    TestTrue(TEXT("duplicate wrote nothing"),F.Read(A).Heights==AfterA.Heights);
    TestEqual(TEXT("one record per request"),AutomationRetryRecordCount(),1);
    // Undo, then the same request: a normal new Apply.
    TestTrue(TEXT("human undo"),GEditor->UndoTransaction());F.Update();TestTrue(TEXT("undo restored"),F.Read(A).Heights==Original.Heights);
    const double ReapplyMs=Timed([&]{O=Execute(A);});
    TestEqual(*O.Message,O.Status,FString(TEXT("APPLIED")));TestTrue(TEXT("reapply verified"),O.bActualValid&&O.ModifiedCount>0);
    TestEqual(TEXT("reapply transaction +1"),UToolsetLibrary::GetActiveUndoCount(),Undo0+1);
    TestTrue(TEXT("reapply is a new owned transaction"),GEditor->Trans->GetUndoContext().TransactionId!=FirstTransaction);
    TestTrue(TEXT("reapply deterministic source"),F.Read(A).Heights==AfterA.Heights);
    TestEqual(TEXT("stale record replaced, not duplicated"),AutomationRetryRecordCount(),1);
    // Undo + Redo returns to the recorded post-state: duplicate again.
    TestTrue(TEXT("undo"),GEditor->UndoTransaction());F.Update();TestTrue(TEXT("redo"),GEditor->RedoTransaction());F.Update();
    O=Execute(A);TestEqual(TEXT("redo state NO_CHANGE"),O.Status,FString(TEXT("NO_CHANGE")));
    TestEqual(TEXT("redo state transaction +0"),UToolsetLibrary::GetActiveUndoCount(),Undo0+1);
    // A -> overlapping B -> A: A is replanned on the current terrain.
    O=Execute(B);if(!TestEqual(*O.Message,O.Status,FString(TEXT("APPLIED"))))return false;
    const auto AfterB=F.Read(A);TestTrue(TEXT("B overlaps A"),AfterB.Heights!=AfterA.Heights);
    FPreparedTerrainPlan Fresh;auto Dry=A;Dry.bDryRun=true;const auto Expected=PlanPreview(Dry,AfterB,&Fresh);
    const double ReplanMs=Timed([&]{O=Execute(A);});
    TestTrue(*O.Message,O.bSuccess);
    TestEqual(TEXT("A after B follows the fresh plan"),O.Status,FString(Expected.PlannedModifiedCount?TEXT("APPLIED"):TEXT("NO_CHANGE")));
    TestEqual(TEXT("A after B fresh modified count"),O.ModifiedCount,Expected.PlannedModifiedCount);
    TestTrue(TEXT("A after B equals fresh plan on current terrain"),F.Read(A).Heights==Fresh.Quantized);
    TestEqual(TEXT("A after B transactions"),UToolsetLibrary::GetActiveUndoCount(),Undo0+2+(Expected.PlannedModifiedCount?1:0));
    FString Error;const bool Collision=VerifyAutomationCollision(F.Path,F.Read(A),Fresh,Error);TestTrue(*Error,Collision);
    // B is now stale as well and is replanned, never refused.
    O=Execute(B);TestTrue(*O.Message,O.bSuccess);TestTrue(TEXT("no stale refusal"),O.ErrorCode.IsEmpty());
    TestEqual(TEXT("two requests, two records"),AutomationRetryRecordCount(),2);
    AddInfo(FString::Printf(TEXT("PERFORMANCE_AMENDMENT retry applyMs=%.3f duplicateNoChangeMs=%.3f undoReapplyMs=%.3f aThenBThenAMs=%.3f"),ApplyMs,DuplicateMs,ReapplyMs,ReplanMs));
    TestFalse(TEXT("no latch"),MutationDisabled());TestFalse(TEXT("transaction idle"),GEditor->IsTransactionActive());TestEqual(TEXT("no saves"),F.Saves,0);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04RetryCapacity,"LandscapeMCP.V04.Retry.CapacityEviction",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04RetryCapacity::RunTest(const FString&)
{
    FFixture F;if(!TestNotNull(TEXT("fixture"),F.Actor))return false;
    auto Make=[&](int32 I){auto R=F.Request(EAuthoringOperation::FlattenRectangle);R.TargetHeight=10+I*0.8;return R;};
    FLandscapeMCPTerrainOperationResult O;double TotalMs=0,MaxMs=0;int32 MaxRecords=0,Failures=0;
    auto Apply=[&](int32 I)
    {
        const double Ms=Timed([&]{O=Execute(Make(I));});TotalMs+=Ms;MaxMs=FMath::Max(MaxMs,Ms);
        MaxRecords=FMath::Max(MaxRecords,AutomationRetryRecordCount());
        if(O.Status!=TEXT("APPLIED")){++Failures;AddError(FString::Printf(TEXT("request %d: %s %s %s"),I,*O.Status,*O.ErrorCode,*O.Message.Left(200)));}
    };
    for(int32 I=0;I<128;++I)Apply(I);
    TestEqual(TEXT("128 distinct requests fill the cache"),AutomationRetryRecordCount(),128);
    const double Fill128Ms=TotalMs;
    Apply(128);
    TestEqual(TEXT("129th request is applied, not refused"),O.Status,FString(TEXT("APPLIED")));
    TestEqual(TEXT("cache stays bounded after eviction"),AutomationRetryRecordCount(),128);
    // Newest record still answers a duplicate.
    const int32 UndoNewest=UToolsetLibrary::GetActiveUndoCount();
    O=Execute(Make(128));TestEqual(TEXT("newest duplicate NO_CHANGE"),O.Status,FString(TEXT("NO_CHANGE")));
    TestEqual(TEXT("newest duplicate transaction +0"),UToolsetLibrary::GetActiveUndoCount(),UndoNewest);
    // Evicted oldest request: no record, so a fresh plan on the current terrain.
    const auto Current=F.Read(Make(0));FPreparedTerrainPlan Fresh;auto Dry=Make(0);Dry.bDryRun=true;const auto Expected=PlanPreview(Dry,Current,&Fresh);
    TestTrue(TEXT("oldest request differs from the current terrain"),Expected.PlannedModifiedCount>0);
    const double EvictedMs=Timed([&]{O=Execute(Make(0));});
    TestEqual(*O.Message,O.Status,FString(TEXT("APPLIED")));TestTrue(TEXT("evicted request verified"),O.bActualValid);
    TestEqual(TEXT("evicted request fresh modified count"),O.ModifiedCount,Expected.PlannedModifiedCount);
    TestEqual(TEXT("evicted request transaction +1"),UToolsetLibrary::GetActiveUndoCount(),UndoNewest+1);
    TestTrue(TEXT("evicted request equals fresh plan"),F.Read(Make(0)).Heights==Fresh.Quantized);
    FString Error;const bool Collision=VerifyAutomationCollision(F.Path,F.Read(Make(0)),Fresh,Error);TestTrue(*Error,Collision);
    TestEqual(TEXT("cache still bounded"),AutomationRetryRecordCount(),128);
    // Hundreds of requests in one module session: bounded memory, no global stop.
    for(int32 I=129;I<400;++I)Apply(I%2?I-128:I);
    TestEqual(TEXT("every request of the stress run applied"),Failures,0);
    TestEqual(TEXT("cache never exceeded its bound"),MaxRecords,128);TestEqual(TEXT("cache bounded at the end"),AutomationRetryRecordCount(),128);
    TestFalse(TEXT("no latch"),MutationDisabled());
    AddInfo(FString::Printf(TEXT("PERFORMANCE_AMENDMENT retryCapacity requests=400 fill128Ms=%.3f meanApplyMs=%.3f maxApplyMs=%.3f evictedReplanMs=%.3f maxRecords=%d"),Fill128Ms,TotalMs/400,MaxMs,EvictedMs,MaxRecords));
    TestEqual(TEXT("no saves"),F.Saves,0);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04DiagnosticStage,"LandscapeMCP.V04.MCP.DiagnosticStage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04DiagnosticStage::RunTest(const FString&)
{
    FFixture F;if(!TestNotNull(TEXT("fixture"),F.Actor))return false;
    auto Check=[&](const TCHAR* Name,FPreviewRequest R,const TCHAR* Code,const TCHAR* ExpectedStage)
    {
        FString Stage;const auto O=Execute(R,&Stage);const FString Text=FormatFailureDiagnostic(O,Stage);
        TSharedPtr<FJsonObject> D;
        if(!TestFalse(Name,O.bSuccess)||!TestTrue(TEXT("diagnostic JSON"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),D)&&D.IsValid()))return FString();
        AddInfo(FString::Printf(TEXT("DIAGNOSTIC %s %s"),Name,*Text));
        TestEqual(*FString::Printf(TEXT("%s errorCode"),Name),D->GetStringField(TEXT("errorCode")),FString(Code));
        TestEqual(*FString::Printf(TEXT("%s stage"),Name),D->GetStringField(TEXT("stage")),FString(ExpectedStage));
        TestEqual(TEXT("operation"),D->GetStringField(TEXT("operation")),O.Operation);TestEqual(TEXT("landscapePath"),D->GetStringField(TEXT("landscapePath")),F.Path);
        TestEqual(TEXT("rollbackStatus"),D->GetStringField(TEXT("rollbackStatus")),O.RollbackStatus);TestEqual(TEXT("mutationDisabled"),D->GetBoolField(TEXT("mutationDisabled")),MutationDisabled());
        TestFalse(TEXT("reason present"),D->GetStringField(TEXT("reason")).IsEmpty());TestTrue(TEXT("bounded"),Text.Len()<6000&&!Text.Contains(TEXT("profileSamples")));
        TestFalse(TEXT("no development phase or old release number in the diagnostic"),Text.Contains(TEXT("Phase"))||Text.Contains(TEXT("PHASE"))||Text.Contains(TEXT("v0.")));
        TestFalse(TEXT("reason does not repeat the stage prefix"),D->GetStringField(TEXT("reason")).StartsWith(FString(ExpectedStage)+TEXT(": ")));
        return D->GetStringField(TEXT("reason"));
    };
    auto Dry=F.Request(EAuthoringOperation::FlattenRectangle);Dry.bDryRun=true;
    auto Yaw=Dry;Yaw.Yaw=360;Check(TEXT("dryRunValidation"),Yaw,TEXT("UNSUPPORTED_RECTANGLE_YAW"),TEXT("validation"));
    auto Delta=Dry;Delta.MaxDelta=1;Check(TEXT("dryRunPlanning"),Delta,TEXT("MAX_DELTA_EXCEEDED"),TEXT("planning"));
    auto ApplyDelta=Delta;ApplyDelta.bDryRun=false;Check(TEXT("applyPlanning"),ApplyDelta,TEXT("MAX_DELTA_EXCEEDED"),TEXT("planning"));
    {
        FScopedTransaction Outer(INVTEXT("Unrelated outer transaction"));F.Sentinel->Modify();
        Check(TEXT("applyResolve"),F.Request(EAuthoringOperation::FlattenRectangle),TEXT("ACTIVE_TRANSACTION"),TEXT("resolve"));
    }
    {
        auto* SavePolicy=GetMutableDefault<UEditorLoadingSavingSettings>();const bool OldAutoSave=SavePolicy->bAutoSaveEnable;SavePolicy->bAutoSaveEnable=true;
        const FString Reason=Check(TEXT("applySavePolicy"),F.Request(EAuthoringOperation::FlattenRectangle),TEXT("UNSUPPORTED_EDITOR_SAVE_POLICY"),TEXT("resolve"));
        SavePolicy->bAutoSaveEnable=OldAutoSave;TestTrue(TEXT("save policy wording"),Reason.StartsWith(TEXT("Apply requires autosave")));
    }
    // A reason that itself contains ": " must not become the stage.
    ULandscapeComponent* C=F.Actor->LandscapeComponents[0];
    AInstancedFoliageActor* IFA=AInstancedFoliageActor::GetInstancedFoliageActorForLevel(F.World->PersistentLevel,true);
    UStaticMesh* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    if(!TestNotNull(TEXT("foliage actor"),IFA)||!TestNotNull(TEXT("mesh"),Mesh))return false;
    UFoliageType* Type=nullptr;FFoliageInfo* Info=IFA->AddMesh(Mesh,&Type);
    if(!TestNotNull(TEXT("foliage info"),Info)||!TestNotNull(TEXT("foliage type"),Type))return false;
    FFoliageInstance Instance;Instance.Location=FVector(3000,3000,0);Instance.BaseComponent=C->GetCollisionComponent();
    Info->AddInstance(Type,Instance,C->GetCollisionComponent());
    const FString DryReason=Check(TEXT("dryRunSnapshotColonReason"),Dry,TEXT("FOLIAGE_SAFETY_CONFLICT"),TEXT("snapshot"));
    TestTrue(TEXT("colon stays inside the reason"),DryReason.Contains(TEXT("unsupported: editing")));
    const FString ApplyReason=Check(TEXT("applyResolveColonReason"),F.Request(EAuthoringOperation::FlattenRectangle),TEXT("FOLIAGE_SAFETY_CONFLICT"),TEXT("resolve"));
    TestTrue(TEXT("apply colon stays inside the reason"),ApplyReason.Contains(TEXT("unsupported: editing")));
    // The public facade/official adapter path is asserted by the MCP transport client.
    TestEqual(TEXT("no saves"),F.Saves,0);return true;
}
#endif
