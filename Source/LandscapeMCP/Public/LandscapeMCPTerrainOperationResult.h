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

/** Foliage影響の1 instance分。分類がUNCHANGED以外のものだけを返す。 */
USTRUCT(BlueprintType)
struct LANDSCAPEMCP_API FLandscapeMCPFoliageImpactSample
{
    GENERATED_BODY()
    /** Foliage TypeのAsset path。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString FoliageType;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FVector Location = FVector::ZeroVector;
    /** FLOATING / BURIED / SLOPE_CHANGED / MAY_BE_MOVED_BY_ENGINE / MAY_BE_DELETED_BY_ENGINE / UNKNOWN。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString Classification;
    /** 付属先。TARGET_LANDSCAPE / OTHER_COMPONENT / UNATTACHED。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString Base;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double CurrentTerrainCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double PlannedTerrainCm = 0;
    /** PlannedTerrainCm - CurrentTerrainCm。負なら地形が下がる（instanceが浮く）。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double DeltaCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double SlopeChangeDegrees = 0;
    /** UNKNOWNの理由。それ以外は空。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString Reason;
};

/** Apply後のFoliage検証。Dry-runではbCheckedがfalse。 */
USTRUCT(BlueprintType)
struct LANDSCAPEMCP_API FLandscapeMCPFoliagePostApply
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bChecked = false;
    /** 範囲内の全instanceの数・位置・回転・Scale・付属先が書き込み前と一致。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bInstancesUnchanged = false;
    /** 各instanceのXYでの実際の地形高度が、Dry-runの予測と一致。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bTerrainMatchesPrediction = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MaxPredictionErrorCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 VerifiedCount = 0;
    /** Transaction外のLandscape更新（遅延更新）を流した後に再確認したか。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bDeferredUpdateChecked = false;
};

/** 地形編集がFoliageへ与える影響。Foliageは動かさない前提の予測。 */
USTRUCT(BlueprintType)
struct LANDSCAPEMCP_API FLandscapeMCPFoliageImpact
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bEvaluated = false;
    /** NO_FOLIAGE / SAFE_TO_APPLY / BLOCKED / UNKNOWN。Applyは NO_FOLIAGE と SAFE_TO_APPLY のときだけ書き込む。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString Verdict;
    /** Project設定 FoliageLandscapeSnappingMode。DontSnap / SnapUnderBrushOnly / SnapEverywhere。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString SnappingMode;
    /** 評価した時点と根拠。DRY_RUN（計画の予測）/ APPLY（このApplyの判定と事後検証）/
     * RETRY_CURRENT_STATE（書き込みのない再要求。今の地形とFoliageを今回あらためて評価したもの。PostApplyは空）。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString Evaluation;
    /** 読み取り範囲（readBounds）内のinstance数。走査上限を超えた場合は件数不明として -1。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 InspectedCount = 0;
    /** 読み取り範囲に重なる候補が走査上限（65,536）を超えた。件数・分類は不完全で、VerdictはUNKNOWN。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bScanLimitExceeded = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 UnchangedCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 FloatingCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 BuriedCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 SlopeChangedCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 MayBeMovedCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 MayBeDeletedCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 UnknownCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MaxFloatCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MaxBuryCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MaxSlopeChangeDegrees = 0;
    /** 分類がUNCHANGED以外のinstance。最大64件。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") TArray<FLandscapeMCPFoliageImpactSample> Samples;
    /** 判定できない形式・状態。1件でもあればVerdictはUNKNOWN。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") TArray<FString> Unknowns;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FLandscapeMCPFoliagePostApply PostApply;
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
    /** Foliageへの影響（Dry-run）と、Apply後のFoliage検証。既存fieldの後ろへの追加のみ。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FLandscapeMCPFoliageImpact Foliage;
};
