#include "LandscapeMCPTerrainAnalysis.h"

namespace LandscapeMCP::Analysis
{
namespace
{
    // これ未満の勾配は水平として扱い、方向を返さない。
    constexpr double MinDirectionalGradient = 1.e-9;
}

FSlope SlopeFromGradient(double GradientX, double GradientY)
{
    FSlope S; S.GradientX = GradientX; S.GradientY = GradientY;
    const double Magnitude = FMath::Sqrt(GradientX*GradientX + GradientY*GradientY);
    S.SlopeDegrees = FMath::RadiansToDegrees(FMath::Atan(Magnitude));
    S.bDirectionValid = Magnitude >= MinDirectionalGradient;
    if (S.bDirectionValid)
    {
        double Direction = FMath::RadiansToDegrees(FMath::Atan2(GradientY, GradientX));
        if (Direction < 0) { Direction += 360.0; }
        S.DirectionDegrees = Direction >= 360.0 ? 0.0 : Direction;
    }
    // 面 z=f(x,y) の上向き法線は (-df/dx, -df/dy, 1) の正規化。
    const double Length = FMath::Sqrt(Magnitude*Magnitude + 1.0);
    S.Normal = FVector(-GradientX/Length, -GradientY/Length, 1.0/Length);
    return S;
}

FSlope SlopeFromHeights(double HeightXMinus, double HeightXPlus, double HeightYMinus, double HeightYPlus, double DistanceCm)
{
    return SlopeFromGradient((HeightXPlus-HeightXMinus)/(2.0*DistanceCm), (HeightYPlus-HeightYMinus)/(2.0*DistanceCm));
}

double SlopeUncertaintyDegrees(const FSlope& Slope, double QuantizationCm, double DistanceCm)
{
    const double GradientError = UE_DOUBLE_SQRT_2 * QuantizationCm / (2.0*DistanceCm);
    const double Squared = Slope.GradientX*Slope.GradientX + Slope.GradientY*Slope.GradientY;
    return FMath::RadiansToDegrees(GradientError / (1.0+Squared));
}

FWalkability EvaluateWalkability(double SlopeDegrees, double WalkableFloorAngleDeg, double UncertaintyDegrees)
{
    FWalkability W;
    W.MarginDegrees = WalkableFloorAngleDeg - SlopeDegrees;
    W.bWalkable = W.MarginDegrees >= 0;
    if (FMath::Abs(W.MarginDegrees) <= UncertaintyDegrees) { W.Classification = EWalkability::NearLimit; }
    else { W.Classification = W.bWalkable ? EWalkability::Walkable : EWalkability::Unwalkable; }
    return W;
}

const TCHAR* ToString(EWalkability Classification)
{
    switch (Classification)
    {
    case EWalkability::Walkable: return TEXT("WALKABLE");
    case EWalkability::NearLimit: return TEXT("NEAR_LIMIT");
    default: return TEXT("UNWALKABLE");
    }
}

FRegionStats ComputeRegionStats(TConstArrayView<double> Heights, int32 CountX, int32 CountY, double SpacingCm)
{
    FRegionStats S;
    if (CountX < 1 || CountY < 1 || Heights.Num() != CountX*CountY) { return S; }
    double Sum = 0;
    for (int32 Index = 0; Index < Heights.Num(); ++Index)
    {
        const double H = Heights[Index]; Sum += H;
        if (S.MinIndex == INDEX_NONE || H < S.MinHeight) { S.MinHeight = H; S.MinIndex = Index; }
        if (S.MaxIndex == INDEX_NONE || H > S.MaxHeight) { S.MaxHeight = H; S.MaxIndex = Index; }
    }
    S.MeanHeight = Sum / Heights.Num();
    if (CountX < 2 || CountY < 2 || !(SpacingCm > 0)) { return S; }
    S.bSlopeValid = true;
    for (int32 Y = 0; Y+1 < CountY; ++Y)
    {
        for (int32 X = 0; X+1 < CountX; ++X)
        {
            const double H00 = Heights[Y*CountX+X], H10 = Heights[Y*CountX+X+1];
            const double H01 = Heights[(Y+1)*CountX+X], H11 = Heights[(Y+1)*CountX+X+1];
            const FSlope Cell = SlopeFromGradient(((H10-H00)+(H11-H01))/(2.0*SpacingCm), ((H01-H00)+(H11-H10))/(2.0*SpacingCm));
            if (S.MaxSlopeCellX == INDEX_NONE || Cell.SlopeDegrees > S.MaxSlopeDegrees)
            { S.MaxSlopeDegrees = Cell.SlopeDegrees; S.MaxSlopeCellX = X; S.MaxSlopeCellY = Y; }
        }
    }
    return S;
}
}
