#pragma once
#include "LandscapeMCPTerrainOperationResult.h"
#include "LandscapeMCPHeightfieldAccess.h"
#include "LandscapeMCPPathSampling.h"

namespace LandscapeMCP::Authoring
{
    enum class EAuthoringOperation { FlattenPath, FlattenRectangle, CreateWalkablePath };
    struct FPreviewRequest
    {
        EAuthoringOperation Operation = EAuthoringOperation::FlattenPath;
        FString LandscapePath;
        TArray<FVector2D> Points;
        FVector2D Center = FVector2D::ZeroVector;
        double SizeX = 0, SizeY = 0, Width = 0, Falloff = 0, MaxDelta = 0;
        FString HeightMode = TEXT("fixed");
        double TargetHeight = 0, MaxSlope = 0, Yaw = 0;
        bool bDryRun = true;
    };
    // Internal frozen plan; never reflected into the public tool schema.
    struct FPreparedTerrainPlan
    {
        TArray<uint16> Quantized;
        TArray<uint8> Affected, Core, Transition;
    };
    bool PlanWalkableProfile(TConstArrayView<FStation> Stations,TConstArrayView<double> Current,
        double MaxDelta,double MaxSlope,double MinHeight,double MaxHeight,TArray<double>& Target);
    bool ValidateRequest(const FPreviewRequest& Request,FPath& Path,FVector2D& Min,FVector2D& Max,FString& Code);
    FLandscapeMCPTerrainOperationResult PlanPreview(const FPreviewRequest& Request,const FHeightfieldSnapshot& Snapshot,FPreparedTerrainPlan* Prepared=nullptr);
    bool VerifyPreparedGeometry(const FPreviewRequest& Request,const FHeightfieldSnapshot& Original,
        const FHeightfieldSnapshot& Actual,const FPreparedTerrainPlan& Plan,FLandscapeMCPTerrainOperationResult& Result);
    // OutFailureStage: validation / snapshot / planning. Never derived from message text.
    FLandscapeMCPTerrainOperationResult Preview(const FPreviewRequest& Request,FString* OutFailureStage=nullptr);
}
