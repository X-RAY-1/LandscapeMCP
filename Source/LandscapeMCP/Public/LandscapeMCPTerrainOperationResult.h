#pragma once
#include "CoreMinimal.h"
#include "LandscapeMCPTerrainOperationResult.generated.h"

USTRUCT(BlueprintType)
struct LANDSCAPEMCP_API FLandscapeMCPBounds
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bValid = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FVector2D Min = FVector2D::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FVector2D Max = FVector2D::ZeroVector;
};

USTRUCT(BlueprintType)
struct LANDSCAPEMCP_API FLandscapeMCPTerrainStats
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bValid = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MinHeightCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MaxHeightCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MeanHeightCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bSlopeValid = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MaxTriangleSlopeDegrees = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MaxSlopeUncertaintyDegrees = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bWalkabilityValid = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double WalkableFloorAngleDeg = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 TriangleCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 WalkableCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 NearLimitCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 UnwalkableCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double WalkableRatio = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double WorstMarginDegrees = 0;
};

USTRUCT(BlueprintType)
struct LANDSCAPEMCP_API FLandscapeMCPDelta
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bValid = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MinDeltaCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MaxDeltaCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MaxRaiseCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MaxLowerCm = 0;
};

USTRUCT(BlueprintType)
struct LANDSCAPEMCP_API FLandscapeMCPProfileSample
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double DistanceCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double WorldX = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double WorldY = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double CurrentHeightCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double TargetHeightCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double PlannedHeightCm = 0;
};

USTRUCT(BlueprintType)
struct LANDSCAPEMCP_API FLandscapeMCPPathPreview
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bValid = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double PathLengthCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double PathWidthCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double FalloffCm = 0;
    /** 実際のstation間隔の最大値。knot保持によりsegmentごとに短くなる。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double ProfileSamplingIntervalCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 ProfileSampleCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") TArray<FLandscapeMCPProfileSample> ProfileSamples;
};

/** 新規v0.4 Tool専用。Phase 1ではPreviewのみ、actual/modifiedは常に無効/0。 */
USTRUCT(BlueprintType)
struct LANDSCAPEMCP_API FLandscapeMCPTerrainOperationResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 SchemaVersion = 1;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bDryRun = true;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString Status = TEXT("REJECTED");
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString Operation;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString LandscapePath;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString ErrorCode;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FLandscapeMCPBounds RequestedBounds;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FLandscapeMCPBounds AffectedBounds;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FLandscapeMCPBounds ReadBounds;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FLandscapeMCPBounds VerificationBounds;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 RequestedSampleCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 AffectedSampleCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 ReadSampleCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 PlannedModifiedCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 ModifiedCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 ClippedSampleCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bClipped = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FLandscapeMCPTerrainStats Before;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FLandscapeMCPTerrainStats PlannedAfter;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FLandscapeMCPTerrainStats ActualAfter;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FLandscapeMCPTerrainStats TransitionBefore;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FLandscapeMCPTerrainStats TransitionPlannedAfter;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FLandscapeMCPTerrainStats TransitionActualAfter;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FLandscapeMCPDelta Delta;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double HeightQuantizationCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bPlanValid = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bActualValid = false;
    /** source geometryの計画のみ。Collision/Character/NavMeshの保証ではない。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bWalkabilityGuaranteed = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FLandscapeMCPPathPreview Path;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") TArray<FString> Warnings;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") TArray<FString> UnsupportedReasons;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString RollbackStatus = TEXT("NOT_NEEDED");
};
