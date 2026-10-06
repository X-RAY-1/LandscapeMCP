#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
#include "LandscapeMCPOperations.h"
#include "LandscapeMCPToolset.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeEditLayer.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Tests/AutomationEditorCommon.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "ToolsetRegistry/ToolsetLibrary.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeMCPSafetyTest,"LandscapeMCP.V01.SafetyAndOperations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLandscapeMCPSafetyTest::RunTest(const FString& Parameters)
{
    using namespace LandscapeMCP;
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    if (!TestNotNull(TEXT("isolated unsaved test world"),World)) { return false; }
    const FString LevelPath = World->PersistentLevel->GetPathName();
    const FVector Scale(100,100,100);
    int32 SaveEvents = 0;
    const FDelegateHandle SaveHandle = FCoreUObjectDelegates::OnObjectPreSave.AddLambda(
        [&](UObject* Object,FObjectPreSaveContext Context)
        { if (Object && Object->GetOutermost() == World->GetOutermost()) { ++SaveEvents; } });
    struct FSaveGuard
    {
        FDelegateHandle Handle;
        ~FSaveGuard() { FCoreUObjectDelegates::OnObjectPreSave.Remove(Handle); }
    } SaveGuard { SaveHandle };
    AActor* Sentinel = World->SpawnActor<AActor>();
    const FTransform SentinelBefore = Sentinel->GetActorTransform();

    TestFalse(TEXT("invalid creation name"),Create(LevelPath,TEXT("bad/name"),FVector::ZeroVector,Scale,2,2,2,7,0,true).bSuccess);
    TestFalse(TEXT("invalid component count"),Create(LevelPath,TEXT("AI_TestLandscape"),FVector::ZeroVector,Scale,17,2,2,7,0,true).bSuccess);
    TestFalse(TEXT("invalid quads"),Create(LevelPath,TEXT("AI_TestLandscape"),FVector::ZeroVector,Scale,2,2,2,8,0,true).bSuccess);
    const int32 UndoBeforeCreate = UToolsetLibrary::GetActiveUndoCount();
    const FLandscapeMCPResult CreateDry = Create(LevelPath,TEXT("AI_TestLandscape"),FVector::ZeroVector,Scale,2,2,2,7,0,true);
    TestTrue(TEXT("create dry run"),CreateDry.bSuccess);
    TestNull(TEXT("create dry run did not spawn"),FindObject<ALandscape>(nullptr,*CreateDry.LandscapePath));
    TestEqual(TEXT("create dry run has no transaction"),UToolsetLibrary::GetActiveUndoCount(),UndoBeforeCreate);
    const FLandscapeMCPResult Created = Create(LevelPath,TEXT("AI_TestLandscape"),FVector::ZeroVector,Scale,2,2,2,7,0,false);
    if (!TestTrue(*Created.Message,Created.bSuccess)) { return false; }
    const FString Path = Created.LandscapePath;
    ALandscape* Actor = FindObject<ALandscape>(nullptr,*Path);
    if (!TestNotNull(TEXT("created Landscape"),Actor)) { return false; }
    TestEqual(TEXT("2x2 components"),Actor->LandscapeComponents.Num(),4);
    TestFalse(TEXT("duplicate name fails"),Create(LevelPath,TEXT("AI_TestLandscape"),FVector::ZeroVector,Scale,2,2,2,7,0,false).bSuccess);
    TestFalse(TEXT("invalid Landscape name fails"),Height(TEXT("MissingLandscape"),1400,1400).bSuccess);
    TestFalse(TEXT("missing exact Landscape path fails"),Height(LevelPath+TEXT(".Missing"),1400,1400).bSuccess);
    TestTrue(TEXT("height query"),Height(Path,1400,1400).bSuccess);
    TestEqual(TEXT("initial height"),Height(Path,1400,1400).HeightCm,0.0);
    TestTrue(TEXT("inclusive edge query"),Height(Path,2800,2800).bSuccess);
    TestFalse(TEXT("outside height query"),Height(Path,2801,1400).bSuccess);

    FEditRequest R; R.LandscapePath=Path; R.Center=FVector2D(1400,1400);
    R.RadiusCm=450; R.Strength=200; R.Falloff=1; R.bDryRun=false;
    FEditRequest Bad=R; Bad.LandscapePath=TEXT("Missing"); TestFalse(TEXT("edit missing name fails"),Edit(Bad).bSuccess);
    Bad=R; Bad.RadiusCm=-1; TestFalse(TEXT("negative radius fails"),Edit(Bad).bSuccess);
    Bad=R; Bad.RadiusCm=0; TestFalse(TEXT("zero radius fails"),Edit(Bad).bSuccess);
    Bad=R; Bad.RadiusCm=5001; TestFalse(TEXT("oversized radius fails"),Edit(Bad).bSuccess);
    Bad=R; Bad.Strength=1001; TestFalse(TEXT("oversized strength fails"),Edit(Bad).bSuccess);
    Bad=R; Bad.Strength=std::numeric_limits<double>::quiet_NaN(); TestFalse(TEXT("NaN strength fails"),Edit(Bad).bSuccess);
    Bad=R; Bad.RadiusCm=std::numeric_limits<double>::infinity(); TestFalse(TEXT("infinite radius fails"),Edit(Bad).bSuccess);
    Bad=R; Bad.Center.X=std::numeric_limits<double>::infinity(); TestFalse(TEXT("infinite centre fails"),Edit(Bad).bSuccess);
    Bad=R; Bad.Falloff=1.1; TestFalse(TEXT("invalid falloff fails"),Edit(Bad).bSuccess);
    Bad=R; Bad.Center.X=-1; TestFalse(TEXT("outside brush centre fails"),Edit(Bad).bSuccess);

    World->GetOutermost()->SetDirtyFlag(false);
    const int32 UndoBeforeDry = UToolsetLibrary::GetActiveUndoCount();
    R.bDryRun=true;
    FLandscapeMCPResult Dry=Edit(R);
    TestTrue(TEXT("sculpt dry run"),Dry.bSuccess);
    TestTrue(TEXT("dry run predicts changed samples"),Dry.ChangedSampleCount>0);
    TestEqual(TEXT("dry run unchanged height"),Height(Path,1400,1400).HeightCm,0.0);
    TestFalse(TEXT("dry run/read do not dirty package"),World->GetOutermost()->IsDirty());
    TestEqual(TEXT("dry run/read do not create Undo entries"),UToolsetLibrary::GetActiveUndoCount(),UndoBeforeDry);
    R.bDryRun=false;
    TestTrue(TEXT("sculpt raise"),Edit(R).bSuccess);
    const double Raised=Height(Path,1400,1400).HeightCm;
    TestTrue(TEXT("sculpt changed height"),Raised>199 && Raised<201);
    TestTrue(TEXT("Undo edit"),GEditor->UndoTransaction());
    TestEqual(TEXT("Undo restored height"),Height(Path,1400,1400).HeightCm,0.0);
    TestTrue(TEXT("sculpt after Undo"),Edit(R).bSuccess);
    R.Center=FVector2D(2200,1400); R.RadiusCm=300; R.bRaise=false; R.Strength=100;
    TestTrue(TEXT("sculpt lower"),Edit(R).bSuccess);
    TestTrue(TEXT("lower changed height"),Height(Path,2200,1400).HeightCm<-99);

    R.Center=FVector2D(1400,1400); R.RadiusCm=60; R.bRaise=true; R.Strength=100;
    TestTrue(TEXT("local peak"),Edit(R).bSuccess);
    const double DifferenceBefore=FMath::Abs(Height(Path,1400,1400).HeightCm-Height(Path,1500,1400).HeightCm);
    R.Operation=EOperation::Smooth; R.RadiusCm=180; R.Strength=1;
    TestTrue(TEXT("smooth"),Edit(R).bSuccess);
    const double DifferenceAfter=FMath::Abs(Height(Path,1400,1400).HeightCm-Height(Path,1500,1400).HeightCm);
    TestTrue(TEXT("smooth reduces local height difference"),DifferenceAfter<DifferenceBefore);
    R.Operation=EOperation::Flatten; R.TargetHeightCm=50; R.Strength=0.5; R.RadiusCm=300;
    const double DistanceBefore=FMath::Abs(Height(Path,1400,1400).HeightCm-50);
    TestTrue(TEXT("flatten"),Edit(R).bSuccess);
    TestTrue(TEXT("flatten approaches target"),FMath::Abs(Height(Path,1400,1400).HeightCm-50)<DistanceBefore);
    Bad=R; Bad.Strength=1.01; TestFalse(TEXT("flatten invalid blend"),Edit(Bad).bSuccess);
    Bad=R; Bad.TargetHeightCm=100000; TestFalse(TEXT("flatten height overflow rejected"),Edit(Bad).bSuccess);
    Bad=R; Bad.Operation=EOperation::Sculpt; Bad.Strength=1000;
    Bad.Center=FVector2D(2800,2800); Bad.bDryRun=true;
    TestTrue(TEXT("edge brush clipped"),Edit(Bad).bClipped);

    ULandscapeEditLayerBase* Layer=Actor->GetEditLayer(0);
    Layer->SetLocked(true,false); TestFalse(TEXT("locked layer fails"),Edit(R).bSuccess); Layer->SetLocked(false,false);
    Actor->SetActorRotation(FRotator(0,10,0)); TestFalse(TEXT("rotated Landscape fails"),Height(Path,1400,1400).bSuccess);
    Actor->SetActorRotation(FRotator::ZeroRotator);
    const FLandscapeMCPResult OtherCreated=Create(LevelPath,TEXT("AI_OtherLandscape"),FVector(4000,0,0),Scale,1,1,1,7,0,false);
    TestTrue(TEXT("independent second Landscape fixture"),OtherCreated.bSuccess);
    ALandscape* OtherActor=FindObject<ALandscape>(nullptr,*OtherCreated.LandscapePath);
    if (OtherActor)
    {
        const FGuid OtherGuid=OtherActor->GetEditLayer(0)->GetGuid();
        FLandscapeLayerComponentData* OtherData=OtherActor->LandscapeComponents[0]->GetLayerData(OtherGuid);
        UTexture2D* SavedTexture=OtherData->HeightmapData.Texture;
        OtherData->HeightmapData.Texture=Actor->LandscapeComponents[0]->GetHeightmap(Layer->GetGuid());
        TestFalse(TEXT("cross-Landscape texture sharing refused"),Height(Path,0,0).bSuccess);
        OtherData->HeightmapData.Texture=SavedTexture;
    }
    TestTrue(TEXT("non-Landscape Actor unchanged"),Sentinel->GetActorTransform().Equals(SentinelBefore));
    TestEqual(TEXT("no Level / Asset save events"),SaveEvents,0);
    TestFalse(TEXT("unsaved map has no disk file"),IFileManager::Get().FileExists(*FPaths::Combine(FPaths::ProjectContentDir(),World->GetName()+TEXT(".umap"))));
    const FString Schema=UToolsetRegistry::GetToolsetJsonSchema(ULandscapeMCPToolset::StaticClass());
    TestTrue(TEXT("official schema includes CreateLandscape"),Schema.Contains(TEXT("CreateLandscape")));
    TestTrue(TEXT("official schema includes FlattenRegion"),Schema.Contains(TEXT("FlattenRegion")));
    AddInfo(Schema);
    return true;
}
#endif
