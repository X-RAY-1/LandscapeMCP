#include "LandscapeMCPFoliageImpact.h"
#include "LandscapeMCPTerrainAnalysis.h"

namespace LandscapeMCP::Foliage
{
const TCHAR* ToString(EBase Base)
{
    switch (Base)
    {
    case EBase::TargetLandscape: return TEXT("TARGET_LANDSCAPE");
    case EBase::OtherComponent: return TEXT("OTHER_COMPONENT");
    default: return TEXT("UNATTACHED");
    }
}

bool FSample::operator==(const FSample& B) const
{
    // 浮動小数も完全一致で比べる。Foliageを書き換えない契約なので、1 bitの差も変化とみなす。
    return FoliageType == B.FoliageType && Mesh == B.Mesh && Location == B.Location && Rotation == B.Rotation
        && Scale == B.Scale && ZOffsetCm == B.ZOffsetCm && Flags == B.Flags && Base == B.Base && UnknownReason == B.UnknownReason;
}

bool SameInstances(const FInventory& A, const FInventory& B)
{
    return A.Samples == B.Samples && A.Unknowns == B.Unknowns && A.bScanLimitExceeded == B.bScanLimitExceeded
        && A.bTargetHasAttachedFoliage == B.bTargetHasAttachedFoliage;
}

bool SameSafetyState(const FInventory& A, const FInventory& B)
{
    return SameInstances(A, B) && A.SnappingMode == B.SnappingMode && A.bSnappingSafe == B.bSnappingSafe && A.ScannedCount == B.ScannedCount;
}

namespace
{
    bool Local(const Authoring::FHeightfieldSnapshot& S, FVector2D World, double& LX, double& LY)
    {
        if (!(S.Scale.X > 0) || !(S.Scale.Y > 0) || S.CountX() < 2 || S.CountY() < 2) { return false; }
        LX = (World.X - S.Origin.X) / S.Scale.X; LY = (World.Y - S.Origin.Y) / S.Scale.Y;
        return FMath::IsFinite(LX) && FMath::IsFinite(LY) && LX >= S.Rect.Min.X && LX <= S.Rect.Max.X && LY >= S.Rect.Min.Y && LY <= S.Rect.Max.Y;
    }
    double At(const Authoring::FHeightfieldSnapshot& S, TConstArrayView<uint16> H, int32 X, int32 Y)
    {
        return S.Decode(H[(Y - S.Rect.Min.Y) * S.CountX() + X - S.Rect.Min.X]);
    }
    // セル内の局所座標(FX,FY)での三角形の高度（重心座標補間）。
    double TriangleHeight(double H00, double H10, double H01, double H11, double FX, double FY, bool bUpper)
    {
        return bUpper ? H00 + FY * (H01 - H00) + FX * (H11 - H01) : H00 + FX * (H10 - H00) + FY * (H11 - H10);
    }
    double TriangleSlope(const Authoring::FHeightfieldSnapshot& S, TConstArrayView<uint16> H, int32 CX, int32 CY, bool bUpper)
    {
        const Analysis::FCellTriangles Cell = Analysis::CellTriangleSlopes(At(S, H, CX, CY), At(S, H, CX + 1, CY), At(S, H, CX, CY + 1), At(S, H, CX + 1, CY + 1), S.Scale.X, S.Scale.Y);
        return bUpper ? Cell.Upper.SlopeDegrees : Cell.Lower.SlopeDegrees;
    }
    void Primary(const Authoring::FHeightfieldSnapshot& S, double LX, double LY, int32& CX, int32& CY, double& FX, double& FY, bool& bUpper)
    {
        CX = FMath::Clamp(FMath::FloorToInt32(LX), S.Rect.Min.X, S.Rect.Max.X - 1);
        CY = FMath::Clamp(FMath::FloorToInt32(LY), S.Rect.Min.Y, S.Rect.Max.Y - 1);
        FX = LX - CX; FY = LY - CY; bUpper = FX < FY;
    }
}

bool TerrainAt(const Authoring::FHeightfieldSnapshot& S, TConstArrayView<uint16> Heights, FVector2D World, double& HeightCm, double& SlopeDegrees)
{
    double LX = 0, LY = 0;
    if (Heights.Num() != S.CountX() * S.CountY() || !Local(S, World, LX, LY)) { return false; }
    int32 CX = 0, CY = 0; double FX = 0, FY = 0; bool bUpper = false;
    Primary(S, LX, LY, CX, CY, FX, FY, bUpper);
    HeightCm = TriangleHeight(At(S, Heights, CX, CY), At(S, Heights, CX + 1, CY), At(S, Heights, CX, CY + 1), At(S, Heights, CX + 1, CY + 1), FX, FY, bUpper);
    SlopeDegrees = TriangleSlope(S, Heights, CX, CY, bUpper);
    return FMath::IsFinite(HeightCm) && FMath::IsFinite(SlopeDegrees);
}

bool ChangeAt(const Authoring::FHeightfieldSnapshot& S, TConstArrayView<uint16> Before, TConstArrayView<uint16> After, FVector2D World, FTerrainChange& Out)
{
    Out = {};
    double BeforeSlope = 0, AfterSlope = 0;
    if (!TerrainAt(S, Before, World, Out.BeforeCm, BeforeSlope) || !TerrainAt(S, After, World, Out.AfterCm, AfterSlope)) { return false; }
    Out.DeltaCm = Out.AfterCm - Out.BeforeCm;
    double LX = 0, LY = 0; Local(S, World, LX, LY);
    // その点を含むセル。格子線の上なら両側のセル。
    const int32 FX0 = FMath::FloorToInt32(LX), FY0 = FMath::FloorToInt32(LY);
    TArray<int32, TInlineAllocator<2>> Xs, Ys;
    Xs.Add(FX0); Ys.Add(FY0);
    if (LX == double(FX0)) { Xs.Add(FX0 - 1); }
    if (LY == double(FY0)) { Ys.Add(FY0 - 1); }
    Out.bComplete = true;
    for (const int32 CX : Xs)
    {
        for (const int32 CY : Ys)
        {
            // Landscapeの外には三角形がない。
            if (CX < S.Extent.Min.X || CY < S.Extent.Min.Y || CX + 1 > S.Extent.Max.X || CY + 1 > S.Extent.Max.Y) { continue; }
            const double U = LX - CX, V = LY - CY;
            for (int32 Index = 0; Index < 2; ++Index)
            {
                const bool bUpper = Index == 1;
                if (bUpper ? !(V >= U) : !(U >= V)) { continue; }
                const bool bInside = CX >= S.Rect.Min.X && CY >= S.Rect.Min.Y && CX + 1 <= S.Rect.Max.X && CY + 1 <= S.Rect.Max.Y;
                if (bInside)
                {
                    Out.SlopeChangeDegrees = FMath::Max(Out.SlopeChangeDegrees, FMath::Abs(TriangleSlope(S, After, CX, CY, bUpper) - TriangleSlope(S, Before, CX, CY, bUpper)));
                    continue;
                }
                // Snapshot外の三角形: Snapshot内にある頂点が変わらなければ、三角形も変わらない。
                const FIntPoint Corners[3] = {FIntPoint(CX, CY), bUpper ? FIntPoint(CX, CY + 1) : FIntPoint(CX + 1, CY), FIntPoint(CX + 1, CY + 1)};
                for (const FIntPoint& P : Corners)
                {
                    if (P.X < S.Rect.Min.X || P.Y < S.Rect.Min.Y || P.X > S.Rect.Max.X || P.Y > S.Rect.Max.Y) { continue; }
                    const int32 I = (P.Y - S.Rect.Min.Y) * S.CountX() + P.X - S.Rect.Min.X;
                    if (Before[I] != After[I]) { Out.bComplete = false; }
                }
            }
        }
    }
    return FMath::IsFinite(Out.DeltaCm) && FMath::IsFinite(Out.SlopeChangeDegrees);
}

EClass Classify(double DeltaCm, double SlopeChangeDegrees, double HalfStepCm, bool bSnapRisk)
{
    if (FMath::Abs(DeltaCm) > HalfStepCm) { return DeltaCm < 0 ? EClass::Floating : EClass::Buried; }
    if (SlopeChangeDegrees > SlopeChangeThresholdDegrees) { return EClass::SlopeChanged; }
    // 地形が変わらなくても、snap modeが有効なら付属instanceはTransaction外で動き得る。
    return bSnapRisk ? EClass::MayBeMoved : EClass::Unchanged;
}

FLandscapeMCPFoliageImpact Evaluate(const Authoring::FHeightfieldSnapshot& S, TConstArrayView<uint16> Planned, const FInventory& Inv)
{
    FLandscapeMCPFoliageImpact O;
    O.bEvaluated = true;
    O.SnappingMode = Inv.SnappingMode;
    O.Unknowns = Inv.Unknowns;
    O.bScanLimitExceeded = Inv.bScanLimitExceeded;
    if (Inv.bScanLimitExceeded)
    {
        O.Unknowns.Add(FString::Printf(TEXT("SCAN_LIMIT_EXCEEDED: more than %d foliage instances overlap the read bounds."), MaxScannedInstances));
    }
    const TConstArrayView<uint16> After = Planned.IsEmpty() ? TConstArrayView<uint16>(S.Heights) : Planned;
    const double HalfStep = S.Scale.Z / 128.0 / 2.0;
    for (const FSample& Sample : Inv.Samples)
    {
        ++O.InspectedCount;
        FLandscapeMCPFoliageImpactSample Report;
        Report.FoliageType = Sample.FoliageType;
        Report.Location = Sample.Location;
        Report.Base = ToString(Sample.Base);
        const FVector2D XY(Sample.Location.X, Sample.Location.Y);
        FTerrainChange Change;
        const bool bReadable = ChangeAt(S, S.Heights, After, XY, Change);
        const bool bSnapRisk = Sample.Base == EBase::TargetLandscape && !Inv.bSnappingSafe;
        if (!Sample.UnknownReason.IsEmpty())
        {
            Report.Classification = TEXT("UNKNOWN"); Report.Reason = Sample.UnknownReason; ++O.UnknownCount;
        }
        else if (!bReadable || !Change.bComplete)
        {
            if (bSnapRisk) { Report.Classification = TEXT("MAY_BE_DELETED_BY_ENGINE"); ++O.MayBeDeletedCount; }
            else { Report.Classification = TEXT("UNKNOWN"); Report.Reason = bReadable ? TEXT("ADJACENT_TRIANGLE_OUTSIDE_READ_BOUNDS") : TEXT("TERRAIN_NOT_READABLE"); ++O.UnknownCount; }
        }
        else
        {
            Report.CurrentTerrainCm = Change.BeforeCm; Report.PlannedTerrainCm = Change.AfterCm;
            Report.DeltaCm = Change.DeltaCm; Report.SlopeChangeDegrees = Change.SlopeChangeDegrees;
            switch (Classify(Change.DeltaCm, Change.SlopeChangeDegrees, HalfStep, bSnapRisk))
            {
            case EClass::Floating: Report.Classification = TEXT("FLOATING"); ++O.FloatingCount; O.MaxFloatCm = FMath::Max(O.MaxFloatCm, -Change.DeltaCm); break;
            case EClass::Buried: Report.Classification = TEXT("BURIED"); ++O.BuriedCount; O.MaxBuryCm = FMath::Max(O.MaxBuryCm, Change.DeltaCm); break;
            case EClass::SlopeChanged: Report.Classification = TEXT("SLOPE_CHANGED"); ++O.SlopeChangedCount; break;
            case EClass::MayBeMoved: Report.Classification = TEXT("MAY_BE_MOVED_BY_ENGINE"); ++O.MayBeMovedCount; break;
            default: Report.Classification = TEXT("UNCHANGED"); ++O.UnchangedCount; break;
            }
            O.MaxSlopeChangeDegrees = FMath::Max(O.MaxSlopeChangeDegrees, Change.SlopeChangeDegrees);
        }
        if (Report.Classification != TEXT("UNCHANGED") && O.Samples.Num() < MaxReportedSamples) { O.Samples.Add(MoveTemp(Report)); }
    }
    // 走査を完了できなかった場合、件数は分からない（0件ではない）。
    if (Inv.bScanLimitExceeded) { O.InspectedCount = -1; }
    const int32 Risky = O.FloatingCount + O.BuriedCount + O.SlopeChangedCount + O.MayBeMovedCount + O.MayBeDeletedCount;
    if (!O.Unknowns.IsEmpty() || O.UnknownCount > 0) { O.Verdict = TEXT("UNKNOWN"); }
    // snap modeは範囲外の付属instanceも動かし得るため、範囲内に無くても拒否する。
    else if (Risky > 0 || (Inv.bTargetHasAttachedFoliage && !Inv.bSnappingSafe)) { O.Verdict = TEXT("BLOCKED"); }
    else if (O.InspectedCount == 0) { O.Verdict = TEXT("NO_FOLIAGE"); }
    else { O.Verdict = TEXT("SAFE_TO_APPLY"); }
    return O;
}
}
