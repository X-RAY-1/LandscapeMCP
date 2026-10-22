#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "LandscapeMCPOperations.h"
#include "LandscapeMCPHeightfieldAccess.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Landscape.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeInfo.h"
#include "ScopedTransaction.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/Package.h"

// Prerequisite probe only. No public tool invokes this writer. Run in an isolated
// unsaved Editor project before enabling any Phase 2 production mutation path.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04UndoPrerequisite,"LandscapeMCP.V04.Phase2Prerequisite.SourceCollisionUndo",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04UndoPrerequisite::RunTest(const FString&)
{
    using namespace LandscapeMCP;
    using namespace LandscapeMCP::Authoring;
    UWorld* W=FAutomationEditorCommonUtils::CreateNewMap();
    if(!TestNotNull(TEXT("isolated world"),W))return false;
    const auto C=Create(W->PersistentLevel->GetPathName(),TEXT("V04UndoPrerequisite"),FVector::ZeroVector,FVector(100),1,1,1,63,0,false);
    if(!TestTrue(*C.Message,C.bSuccess))return false;
    ALandscape* A=FindObject<ALandscape>(nullptr,*C.LandscapePath);
    if(!TestNotNull(TEXT("landscape"),A))return false;
    FHeightfieldSnapshot Original;FString Code,Error;
    const FVector2D Min(2400,2400),Max(3600,3600);
    if(!TestTrue(*Error,ReadSnapshot(C.LandscapePath,Min,Max,Original,Code,Error)))return false;
    TArray<uint16> Planned=Original.Heights;
    for(int32 Y=Original.Rect.Min.Y;Y<=Original.Rect.Max.Y;++Y)
        for(int32 X=Original.Rect.Min.X;X<=Original.Rect.Max.X;++X)
            if(X>=26&&X<=34&&Y>=26&&Y<=34)Planned[(Y-Original.Rect.Min.Y)*Original.CountX()+X-Original.Rect.Min.X]+=256;
    const FGuid Layer=A->GetEditLayer(0)->GetGuid();
    auto Update=[&](){A->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Heightmap_All);A->ForceUpdateLayersContent();};
    auto Trace=[&](const TCHAR* Stage,double Expected)
    {
        FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(V04UndoPrerequisite),true);
        const bool HitOK=W->LineTraceSingleByChannel(Hit,FVector(3025,3010,1000),FVector(3025,3010,-1000),ECC_WorldStatic,Q);
        AddInfo(FString::Printf(TEXT("PREREQUISITE %s collisionHit=%d height=%.9f normalZ=%.9f expected=%.9f"),Stage,HitOK,Hit.ImpactPoint.Z,Hit.ImpactNormal.Z,Expected));
        TestTrue(Stage,HitOK&&Hit.GetActor()==A);
        TestEqual(Stage,double(Hit.ImpactPoint.Z),Expected,0.1);
        TestEqual(Stage,double(Hit.ImpactNormal.Z),1.0,1.e-5);
    };
    Trace(TEXT("before"),0);
    FGuid Owned;
    {
        FScopedTransaction T(TEXT("LandscapeMCP.V04.Prerequisite"),INVTEXT("Landscape MCP v0.4 prerequisite"),A);
        Owned=GUndo->GetContext().TransactionId;
        A->Modify();A->GetLandscapeInfo()->Modify();
        {
            FScopedSetLandscapeEditingLayer L(A,Layer);
            FLandscapeEditDataInterface E(A->GetLandscapeInfo(),Layer);
            E.SetHeightData(Original.Rect.Min.X,Original.Rect.Min.Y,Original.Rect.Max.X,Original.Rect.Max.Y,Planned.GetData(),0,false,
                nullptr,nullptr,nullptr,false,nullptr,nullptr,true,true,true);
            E.Flush();
        }
        Update();
    }
    FHeightfieldSnapshot Actual;
    TestTrue(TEXT("fresh apply read"),ReadSnapshot(C.LandscapePath,Min,Max,Actual,Code,Error));
    TestTrue(TEXT("entire source plus halo equals plan"),Actual.Heights==Planned);
    Trace(TEXT("after apply"),200);
    const auto Latest=GEditor->Trans->GetUndoContext();
    const bool Own=Owned.IsValid()&&Latest.TransactionId==Owned;
    AddInfo(FString::Printf(TEXT("PREREQUISITE transaction owned=%s latest=%s match=%d"),*Owned.ToString(),*Latest.TransactionId.ToString(),Own));
    if(!TestTrue(TEXT("owned transaction is latest"),Own))return false;
    TestTrue(TEXT("human undo returns success"),GEditor->UndoTransaction());
    Update();
    TestTrue(TEXT("fresh undo read"),ReadSnapshot(C.LandscapePath,Min,Max,Actual,Code,Error));
    TestTrue(TEXT("undo entire source plus halo restored"),Actual.Heights==Original.Heights);
    Trace(TEXT("after undo"),0);
    TestTrue(TEXT("redo returns success"),GEditor->RedoTransaction());
    Update();
    TestTrue(TEXT("fresh redo read"),ReadSnapshot(C.LandscapePath,Min,Max,Actual,Code,Error));
    TestTrue(TEXT("redo entire source plus halo equals plan"),Actual.Heights==Planned);
    Trace(TEXT("after redo"),200);
    if(GEditor->Trans->GetUndoContext().TransactionId==Owned){TestTrue(TEXT("cleanup own undo"),GEditor->UndoTransaction(false));Update();}
    TestFalse(TEXT("no active transaction"),GEditor->IsTransactionActive());
    return true;
}
#endif
