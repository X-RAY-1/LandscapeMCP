#pragma once
#include "CoreMinimal.h"

// 地形評価の計算層。Landscape・MCP・UObjectへ依存せず、高さの数値だけを入力とする。
// FindWalkablePathや自動Smoothなど、後続機能から同じ定義を再利用できるようにする。
namespace LandscapeMCP::Analysis
{
    struct FSlope
    {
        double GradientX = 0; // dZ/dX。ワールドcm同士の比なので無次元。
        double GradientY = 0; // dZ/dY。
        double SlopeDegrees = 0; // 0は水平、90は垂直。
        double DirectionDegrees = 0; // 最大上昇方向。+Xが0、+Yが90、範囲は[0,360)。
        bool bDirectionValid = false; // 勾配がほぼ0の場合は方向を定義しない。
        FVector Normal = FVector::UpVector; // 上向きの単位法線。
    };

    /** 2D勾配から傾斜角・最大上昇方向・法線を求める。 */
    FSlope SlopeFromGradient(double GradientX, double GradientY);

    /** 中心差分。各高さは中心からDistanceCmだけ離れた4点のワールドZ。 */
    FSlope SlopeFromHeights(double HeightXMinus, double HeightXPlus, double HeightYMinus, double HeightYPlus, double DistanceCm);

    /** 高さ量子化が中心差分の傾斜角へ与え得る誤差の保守的な上限(度)。
     * 各サンプルの誤差はQuantizationCm/2以下なので、軸ごとの勾配誤差はQuantizationCm/(2*DistanceCm)以下、
     * 2軸合成で e = sqrt(2)*QuantizationCm/(2*DistanceCm)。勾配の大きさgは[max(0,g-e), g+e]に収まり、
     * atanは単調なので max(atan(g+e)-atan(g), atan(g)-atan(max(0,g-e))) が角度誤差の上限となる。
     */
    double SlopeUncertaintyDegrees(const FSlope& Slope, double QuantizationCm, double DistanceCm);

    /** 勾配誤差の上限GradientErrorが分かっている場合の傾斜角誤差上限(度)。SlopeUncertaintyDegreesと同じ式。 */
    double SlopeUncertaintyFromGradientError(double GradientMagnitude, double GradientError);

    enum class EWalkability { Walkable, NearLimit, Unwalkable };
    struct FWalkability
    {
        bool bWalkable = false; // SlopeDegrees <= WalkableFloorAngleDeg。境界値は歩行可能側。
        double MarginDegrees = 0; // WalkableFloorAngleDeg - SlopeDegrees。負なら超過。
        EWalkability Classification = EWalkability::Unwalkable;
    };

    /** 傾斜角と歩行可能角を比較する。|Margin| <= UncertaintyDegreesの場合は量子化誤差内で判定が反転し得るためNearLimitとする。 */
    FWalkability EvaluateWalkability(double SlopeDegrees, double WalkableFloorAngleDeg, double UncertaintyDegrees);

    const TCHAR* ToString(EWalkability Classification);

    struct FRegionStats
    {
        double MinHeight = 0;
        double MaxHeight = 0;
        double MeanHeight = 0;
        int32 MinIndex = INDEX_NONE;
        int32 MaxIndex = INDEX_NONE;
        bool bSlopeValid = false; // 各軸2サンプル以上ある場合だけセル傾斜を評価する。
        double MaxSlopeDegrees = 0;
        int32 MaxSlopeCellX = INDEX_NONE;
        int32 MaxSlopeCellY = INDEX_NONE;
    };

    /** 行優先(Yが外側、Xが内側)の等間隔格子を集計する。セル傾斜は4隅の前進差分の平均から求める。 */
    FRegionStats ComputeRegionStats(TConstArrayView<double> Heights, int32 CountX, int32 CountY, double SpacingCm);

    /** Landscapeの1セルを構成する2つの三角形。
     * UE5.8の描画(LandscapeRender.cppのindex順)とCollision(Chaos FHeightField)はどちらも、
     * セルの4頂点(00,10,01,11)を対角線00-11で分割する。Collisionのline traceによる実測でも確認している。
     * Lower=(00,10,11)でセル内の重心は(2/3,1/3)、Upper=(00,01,11)で重心は(1/3,2/3)。
     */
    struct FCellTriangles
    {
        FSlope Lower;
        FSlope Upper;
    };
    FCellTriangles CellTriangleSlopes(double H00, double H10, double H01, double H11, double SpacingX, double SpacingY);

    /** 三角形の重心。格子単位の座標で返す。 */
    FVector2D TriangleCentroid(int32 CellX, int32 CellY, bool bUpper);

    /** 三角形の勾配誤差の上限。各軸の勾配は隣接2頂点の差なので誤差はQuantizationCm/Spacing以下、2軸を合成する。 */
    double TriangleGradientError(double QuantizationCm, double SpacingX, double SpacingY);

    struct FTriangleStats
    {
        int32 TriangleCount = 0;
        double MaxSlopeDegrees = 0;
        double MeanSlopeDegrees = 0; // 三角形はXY面積が等しいため、単純平均が面積加重平均になる。
        FSlope MaxSlope;
        int32 MaxCellX = INDEX_NONE;
        int32 MaxCellY = INDEX_NONE;
        bool bMaxIsUpper = false;
        double MaxSlopeUncertaintyDegrees = 0;
        // WalkableFloorAngleDegに対する三角形ごとの分類数。合計はTriangleCount。
        int32 WalkableCount = 0;
        int32 NearLimitCount = 0;
        int32 UnwalkableCount = 0;
    };

    /** 行優先(Yが外側、Xが内側)の頂点格子上の三角形を集計する。
     * Includeは(CellX, CellY, bUpper)を受け取り、falseを返した三角形を除外する。
     * 分類はEvaluateWalkabilityと同じ規則で、不確かさは三角形ごとの量子化誤差上限を使う。
     */
    FTriangleStats ComputeTriangleStats(TConstArrayView<double> Heights, int32 CountX, int32 CountY, double SpacingX, double SpacingY,
        double WalkableFloorAngleDeg, double QuantizationCm, TFunctionRef<bool(int32, int32, bool)> Include);

    enum class ERegionWalkability { Walkable, Mixed, Unwalkable };
    /** 全三角形がWALKABLEならWalkable、全三角形がUNWALKABLEならUnwalkable、それ以外(NEAR_LIMITを含む)はMixed。 */
    ERegionWalkability ClassifyRegion(const FTriangleStats& Stats);
    const TCHAR* ToString(ERegionWalkability Classification);
}
