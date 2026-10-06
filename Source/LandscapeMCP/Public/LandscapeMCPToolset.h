#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "LandscapeMCPToolset.generated.h"

/** 高さと距離の数値はワールド座標のcm。Dry-runではTransactionを開始しない。 */
USTRUCT(BlueprintType)
struct LANDSCAPEMCP_API FLandscapeMCPResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bDryRun = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString LandscapePath;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 SampleCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 ChangedSampleCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FVector WorldMin = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FVector WorldMax = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MinDeltaCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MaxDeltaCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double HeightCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double HeightQuantizationCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bClipped = false;
};

/** GetSlopeの結果。角度は度、距離と高さはワールド座標のcm。読み取り専用で、PackageやUndo履歴を変更しない。 */
USTRUCT(BlueprintType)
struct LANDSCAPEMCP_API FLandscapeMCPSlopeResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString LandscapePath;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double WorldX = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double WorldY = 0;
    /** 0は水平、90は垂直。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double SlopeDegrees = 0;
    /** 最大上昇方向。+Xが0、+Yが90、範囲は[0,360)。bDirectionValidがfalseなら0で意味を持たない。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double SlopeDirectionDegrees = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bDirectionValid = false;
    /** 上向きの単位法線。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FVector Normal = FVector::UpVector;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double SampleDistanceCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double HeightCenterCm = 0;
    /** 高さ量子化が傾斜角へ与え得る最大誤差。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double SlopeUncertaintyDegrees = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double HeightQuantizationCm = 0;
};

/** GetHeightRegionの結果。HeightsCmは行優先(Yが外側、Xが内側)で、要素(ix,iy)はWorldMin.XY + (ix,iy)*SampleSpacingCmの高さ。 */
USTRUCT(BlueprintType)
struct LANDSCAPEMCP_API FLandscapeMCPHeightRegionResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString LandscapePath;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 SampleCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 SampleCountX = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 SampleCountY = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double SampleSpacingCm = 0;
    /** 実際にサンプルした格子の最小隅と最小高度。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FVector WorldMin = FVector::ZeroVector;
    /** 実際にサンプルした格子の最大隅と最大高度。間隔が割り切れない場合は要求したmaxより内側になる。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FVector WorldMax = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MinHeightCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MaxHeightCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MeanHeightCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FVector MinHeightLocation = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FVector MaxHeightLocation = FVector::ZeroVector;
    /** 隣接サンプル4点で作るセルごとの傾斜の最大値。各軸2サンプル未満ならbSlopeValidがfalse。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bSlopeValid = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MaxSlopeDegrees = 0;
    /** 最大傾斜セルの中心。Zは4隅の平均高度。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FVector MaxSlopeLocation = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") TArray<double> HeightsCm;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double HeightQuantizationCm = 0;
    /** v0.3: 矩形に重なるLandscapeの実三角形(対角線00-11で分割)の最大傾斜。SampleSpacingCmに依存しない。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bTriangleSlopeValid = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MaxTriangleSlopeDegrees = 0;
    /** 最大傾斜の三角形の重心。Zは3頂点の平均高度。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FVector MaxTriangleSlopeLocation = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 TriangleCount = 0;
};

/** AnalyzeSlopeNeighborhoodの結果。中心傾斜(GetSlopeと同じ中心差分)と、周囲の実三角形から求めた局所傾斜を並べて返す。 */
USTRUCT(BlueprintType)
struct LANDSCAPEMCP_API FLandscapeMCPSlopeNeighborhoodResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString LandscapePath;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double WorldX = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double WorldY = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double RadiusCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double SampleDistanceCm = 0;
    /** GetSlopeと同じ定義の代表傾斜。頂点・尾根・谷底・鞍部では周囲が急でも0になり得る。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double CenterSlopeDegrees = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double CenterSlopeDirectionDegrees = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bCenterDirectionValid = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double CenterSlopeUncertaintyDegrees = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double HeightCenterCm = 0;
    /** 重心が半径内にある実三角形(中心を含むセルの2枚は常に含む)の最大傾斜。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double LocalMaxSlopeDegrees = 0;
    /** 同じ三角形群の平均傾斜。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double LocalMeanSlopeDegrees = 0;
    /** 最大傾斜の三角形の重心。Zは3頂点の平均高度。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FVector MaxSlopeLocation = FVector::ZeroVector;
    /** 最大傾斜の三角形の最大上昇方向。bMaxSlopeDirectionValidがfalseなら0で意味を持たない。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MaxSlopeDirectionDegrees = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bMaxSlopeDirectionValid = false;
    /** 最大傾斜の三角形について、高さ量子化が傾斜角へ与え得る誤差の上限。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double LocalMaxSlopeUncertaintyDegrees = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 TriangleCount = 0;
    /** 読み取ったHeightfield頂点数。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 SampleCount = 0;
    /** 半径がLandscape端を越え、評価範囲が端で切られた場合true。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bClipped = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double HeightQuantizationCm = 0;
};

/** EvaluateWalkabilityRegionの結果。矩形に重なる実三角形ごとにWalkableFloorAngleDegと比較した集計。 */
USTRUCT(BlueprintType)
struct LANDSCAPEMCP_API FLandscapeMCPWalkabilityRegionResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString LandscapePath;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double WalkableFloorAngleDeg = 0;
    /** 読み取ったHeightfield頂点数。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 SampleCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 TriangleCount = 0;
    /** 三角形ごとの分類数。合計はTriangleCount。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 WalkableCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 NearLimitCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 UnwalkableCount = 0;
    /** WalkableCount / TriangleCount。NEAR_LIMITは含めない。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double WalkableRatio = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MaxSlopeDegrees = 0;
    /** WalkableFloorAngleDeg - MaxSlopeDegrees。負なら超過。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double WorstMarginDegrees = 0;
    /** 最大傾斜の三角形の重心。Zは3頂点の平均高度。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FVector WorstLocation = FVector::ZeroVector;
    /** 最大傾斜の三角形の最大上昇方向。bWorstSlopeDirectionValidがfalseなら0で意味を持たない。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double WorstSlopeDirectionDegrees = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bWorstSlopeDirectionValid = false;
    /** 最大傾斜の三角形について、高さ量子化が傾斜角へ与え得る誤差の上限。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double SlopeUncertaintyDegrees = 0;
    /** WALKABLE(全三角形がWALKABLE) / UNWALKABLE(全三角形がUNWALKABLE) / MIXED(それ以外。NEAR_LIMITを含む場合もMIXED)。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString Classification;
    /** 評価した三角形群のXY範囲(セル単位へ広げた範囲)と高度範囲。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FVector WorldMin = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FVector WorldMax = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double HeightQuantizationCm = 0;
};

/** EvaluateWalkabilityの結果。Landscape形状だけの評価で、CharacterMovementの設定や挙動は参照しない。 */
USTRUCT(BlueprintType)
struct LANDSCAPEMCP_API FLandscapeMCPWalkabilityResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString LandscapePath;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double WorldX = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double WorldY = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double SlopeDegrees = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double SlopeDirectionDegrees = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bDirectionValid = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double WalkableFloorAngleDeg = 0;
    /** SlopeDegrees <= WalkableFloorAngleDeg。境界値は歩行可能側。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bWalkable = false;
    /** WalkableFloorAngleDeg - SlopeDegrees。負なら超過。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MarginDegrees = 0;
    /** WALKABLE / NEAR_LIMIT / UNWALKABLE。NEAR_LIMITは|MarginDegrees| <= SlopeUncertaintyDegrees。 */
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString Classification;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double SlopeUncertaintyDegrees = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double SampleDistanceCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double HeightCenterCm = 0;
};

/** v0.3: Editor限定。World Partition非使用で、表示・ロック解除済みの標準Edit Layerが1つあるLandscapeのみ対応。保存しない。 */
UCLASS()
class LANDSCAPEMCP_API ULandscapeMCPToolset : public UToolsetDefinition
{
    GENERATED_BODY()
public:
    virtual FString GetToolsetVersion() const override { return TEXT("0.3"); }

    /** 明示指定された読み込み済みEditor LevelにLandscapeを作成する。LocationはXY最小隅。
     * Scaleはグリッド座標単位あたりのcm。推奨値は(100,100,100)。InitialWorldHeightは絶対ワールドZのcm。
     * 上限: 16 Component、262144 Sample、各軸1000m。QuadsPerSectionは7,15,31,63。
     * LevelPathは読み込み済みULevelの完全Object Path。Dry-runの既定値はtrue。ディスクへ書き込まない。
     */
    UFUNCTION(meta=(AICallable, NonTransactableToolCall), Category="Landscape MCP")
    static FLandscapeMCPResult CreateLandscape(const FString& LevelPath, const FString& LandscapeName,
        FVector Location, FVector Scale, int32 ComponentCountX, int32 ComponentCountY,
        int32 SectionsPerComponent, int32 QuadsPerSection, double InitialWorldHeight, bool bDryRun = true);

    /** 単一標準Layerの元Heightfieldから、bilinear補間した絶対ワールドZ高度をcmで取得する。
     * LandscapePathは読み込み済みActorの完全Object Path。名前やラベルから推測しない。
     * Texture変更、Layer統合、Transaction作成、保存を行わない。
     */
    UFUNCTION(meta=(AICallable, NonTransactableToolCall), Category="Landscape MCP")
    static FLandscapeMCPResult GetHeight(const FString& LandscapePath, double WorldX, double WorldY);

    /** 円形範囲の高度を加減する。StrengthCmは[0,1000]、bRaiseで符号を選ぶ。RadiusCmは(0,5000]。
     * Falloffは[0,1]で、半径のうち滑らかな縁が占める割合。読み取りは最大16384 Sample。
     * Landscape端で円をclipする。範囲外の中心、穴、非対応対象は拒否する。
     */
    UFUNCTION(meta=(AICallable, NonTransactableToolCall), Category="Landscape MCP")
    static FLandscapeMCPResult SculptRegion(const FString& LandscapePath, FVector2D Center,
        double RadiusCm, double StrengthCm, double Falloff, bool bRaise, bool bDryRun = true);

    /** 3x3近傍平均を同時に1回適用する。Strengthは[0,1]。SculptRegionと同じ制限付き円形ブラシ。 */
    UFUNCTION(meta=(AICallable, NonTransactableToolCall), Category="Landscape MCP")
    static FLandscapeMCPResult SmoothRegion(const FString& LandscapePath, FVector2D Center,
        double RadiusCm, double Strength, double Falloff, bool bDryRun = true);

    /** 絶対高度TargetHeightCmへ近づける。Strengthは[0,1]。同じ制限付き円形ブラシ。 */
    UFUNCTION(meta=(AICallable, NonTransactableToolCall), Category="Landscape MCP")
    static FLandscapeMCPResult FlattenRegion(const FString& LandscapePath, FVector2D Center,
        double RadiusCm, double TargetHeightCm, double Strength, double Falloff, bool bDryRun = true);

    /** 指定World XYの傾斜を、元Heightfieldのbilinear高度の中心差分から取得する。読み取り専用。
     * SampleDistanceCmは中心から+-X/+-Yへ離す距離で[1,5000]。4点すべてがLandscape内に必要で、端ではclipせずFAILする。
     * SlopeDegreesは0が水平、90が垂直。SlopeDirectionDegreesは最大上昇方向(+Xが0、+Yが90)。
     */
    UFUNCTION(meta=(AICallable, NonTransactableToolCall), Category="Landscape MCP")
    static FLandscapeMCPSlopeResult GetSlope(const FString& LandscapePath, double WorldX, double WorldY, double SampleDistanceCm);

    /** 矩形領域の高度をSampleSpacingCm間隔の格子でまとめて取得し、最小・最大・平均と最大傾斜セルを返す。読み取り専用。
     * 矩形全体がLandscape内に必要。SampleSpacingCmは[1,5000]、Sample数は最大1024。超える場合は間隔を広げるか領域を分割する。
     * min > maxの逆転、NaN/Infinity、範囲外はFAILする。
     */
    UFUNCTION(meta=(AICallable, NonTransactableToolCall), Category="Landscape MCP")
    static FLandscapeMCPHeightRegionResult GetHeightRegion(const FString& LandscapePath, double MinX, double MinY,
        double MaxX, double MaxY, double SampleSpacingCm);

    /** 指定World XYの傾斜をWalkableFloorAngleDegと比較するgeometry評価。読み取り専用で、CharacterやBlueprintを参照・変更しない。
     * WalkableFloorAngleDegは[0,90]で呼び出し側が指定する。傾斜の定義と制約はGetSlopeと同じ。
     * 歩行の成否を保証するものではなく、CharacterMovementの完全な再現でもない。
     */
    UFUNCTION(meta=(AICallable, NonTransactableToolCall), Category="Landscape MCP")
    static FLandscapeMCPWalkabilityResult EvaluateWalkability(const FString& LandscapePath, double WorldX, double WorldY,
        double WalkableFloorAngleDeg, double SampleDistanceCm);

    /** 指定位置の中心傾斜と、周囲のLandscape実三角形から求めた局所傾斜を取得する。読み取り専用。
     * CenterSlopeDegreesはGetSlopeと同じ中心差分(SampleDistanceCmは[1,5000])。頂点・尾根・谷底・鞍部では0になり得る。
     * LocalMaxSlopeDegrees / LocalMeanSlopeDegreesは、重心がRadiusCm((0,5000])以内にある実三角形の最大・平均傾斜。
     * 読み取りは最大16384頂点。半径がLandscape端を越える部分は評価範囲から外れ、bClippedがtrueになる。
     */
    UFUNCTION(meta=(AICallable, NonTransactableToolCall), Category="Landscape MCP")
    static FLandscapeMCPSlopeNeighborhoodResult AnalyzeSlopeNeighborhood(const FString& LandscapePath, double WorldX, double WorldY,
        double RadiusCm, double SampleDistanceCm);

    /** 矩形領域に重なるLandscape実三角形を、WalkableFloorAngleDeg([0,90])に対して一括評価するgeometry評価。読み取り専用。
     * 矩形全体がLandscape内に必要。読み取りは最大16384頂点で、超える場合は領域を分割する。
     * CharacterやBlueprintを参照せず、CharacterMovementの完全な再現でもない。
     */
    UFUNCTION(meta=(AICallable, NonTransactableToolCall), Category="Landscape MCP")
    static FLandscapeMCPWalkabilityRegionResult EvaluateWalkabilityRegion(const FString& LandscapePath, double MinX, double MinY,
        double MaxX, double MaxY, double WalkableFloorAngleDeg);
};
