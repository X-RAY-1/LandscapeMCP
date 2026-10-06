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
}
