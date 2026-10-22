#pragma once
#include "LandscapeMCPTerrainPlanning.h"

namespace LandscapeMCP::Authoring
{
    // OutFailureStage receives the processing stage that rejected the request.
    FLandscapeMCPTerrainOperationResult Execute(const FPreviewRequest& Request,FString* OutFailureStage=nullptr);
    bool MutationDisabled();
    // Bounded JSON diagnostic for a failed result. Stage is supplied by the
    // caller of Execute; reason text never decides the stage.
    FString FormatFailureDiagnostic(const FLandscapeMCPTerrainOperationResult& Result,const FString& Stage);
#if WITH_DEV_AUTOMATION_TESTS
    // Internal Automation-only fault seam. No reflection, console command or MCP API.
    enum class EApplyFault { None, SourceMismatch, FreshReadFailure, GeometryMismatch, CollisionFailure,
        UndoFalse, RestoreMismatch, RestoreCollisionFailure, OwnershipMismatch, InsertUnrelatedTransaction, StaleSource, RestoreCodecMismatch, RestoreCollisionStale };
    bool VerifyAutomationCollision(const FString& Path,const FHeightfieldSnapshot& Source,const FPreparedTerrainPlan& Plan,FString& Error);
    void SetAutomationApplyFault(EApplyFault Fault);
    void ResetAutomationApplyState();
    int32 AutomationRetryRecordCount();
#endif
}
