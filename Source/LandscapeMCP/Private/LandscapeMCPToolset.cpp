#include "LandscapeMCPToolset.h"
#include "LandscapeMCPOperations.h"
#include "Kismet/KismetSystemLibrary.h"

namespace
{
    template <typename TResult>
    TResult Publish(TResult Result)
    {
        if (!Result.bSuccess) { UKismetSystemLibrary::RaiseScriptError(TEXT("LandscapeMCP: ") + Result.Message); }
        return Result;
    }
}

FLandscapeMCPResult ULandscapeMCPToolset::CreateLandscape(const FString& LevelPath, const FString& LandscapeName,
    FVector Location, FVector Scale, int32 ComponentCountX, int32 ComponentCountY, int32 SectionsPerComponent,
    int32 QuadsPerSection, double InitialWorldHeight, bool bDryRun)
{
    return Publish(LandscapeMCP::Create(LevelPath, LandscapeName, Location, Scale, ComponentCountX,
        ComponentCountY, SectionsPerComponent, QuadsPerSection, InitialWorldHeight, bDryRun));
}

FLandscapeMCPResult ULandscapeMCPToolset::GetHeight(const FString& LandscapePath, double WorldX, double WorldY)
{
    return Publish(LandscapeMCP::Height(LandscapePath, WorldX, WorldY));
}

FLandscapeMCPResult ULandscapeMCPToolset::SculptRegion(const FString& LandscapePath, FVector2D Center,
    double RadiusCm, double StrengthCm, double Falloff, bool bRaise, bool bDryRun)
{
    LandscapeMCP::FEditRequest R;
    R.LandscapePath = LandscapePath; R.Center = Center; R.RadiusCm = RadiusCm;
    R.Strength = StrengthCm; R.Falloff = Falloff; R.bRaise = bRaise; R.bDryRun = bDryRun;
    return Publish(LandscapeMCP::Edit(R));
}

FLandscapeMCPResult ULandscapeMCPToolset::SmoothRegion(const FString& LandscapePath, FVector2D Center,
    double RadiusCm, double Strength, double Falloff, bool bDryRun)
{
    LandscapeMCP::FEditRequest R;
    R.LandscapePath = LandscapePath; R.Center = Center; R.RadiusCm = RadiusCm;
    R.Strength = Strength; R.Falloff = Falloff; R.bDryRun = bDryRun; R.Operation = LandscapeMCP::EOperation::Smooth;
    return Publish(LandscapeMCP::Edit(R));
}

FLandscapeMCPResult ULandscapeMCPToolset::FlattenRegion(const FString& LandscapePath, FVector2D Center,
    double RadiusCm, double TargetHeightCm, double Strength, double Falloff, bool bDryRun)
{
    LandscapeMCP::FEditRequest R;
    R.LandscapePath = LandscapePath; R.Center = Center; R.RadiusCm = RadiusCm;
    R.Strength = Strength; R.Falloff = Falloff; R.TargetHeightCm = TargetHeightCm;
    R.bDryRun = bDryRun; R.Operation = LandscapeMCP::EOperation::Flatten;
    return Publish(LandscapeMCP::Edit(R));
}

FLandscapeMCPSlopeResult ULandscapeMCPToolset::GetSlope(const FString& LandscapePath, double WorldX, double WorldY, double SampleDistanceCm)
{
    return Publish(LandscapeMCP::Slope(LandscapePath, WorldX, WorldY, SampleDistanceCm));
}

FLandscapeMCPHeightRegionResult ULandscapeMCPToolset::GetHeightRegion(const FString& LandscapePath, double MinX, double MinY,
    double MaxX, double MaxY, double SampleSpacingCm)
{
    return Publish(LandscapeMCP::HeightRegion(LandscapePath, MinX, MinY, MaxX, MaxY, SampleSpacingCm));
}

FLandscapeMCPWalkabilityResult ULandscapeMCPToolset::EvaluateWalkability(const FString& LandscapePath, double WorldX, double WorldY,
    double WalkableFloorAngleDeg, double SampleDistanceCm)
{
    return Publish(LandscapeMCP::Walkability(LandscapePath, WorldX, WorldY, WalkableFloorAngleDeg, SampleDistanceCm));
}

FLandscapeMCPSlopeNeighborhoodResult ULandscapeMCPToolset::AnalyzeSlopeNeighborhood(const FString& LandscapePath, double WorldX, double WorldY,
    double RadiusCm, double SampleDistanceCm)
{
    return Publish(LandscapeMCP::SlopeNeighborhood(LandscapePath, WorldX, WorldY, RadiusCm, SampleDistanceCm));
}

FLandscapeMCPWalkabilityRegionResult ULandscapeMCPToolset::EvaluateWalkabilityRegion(const FString& LandscapePath, double MinX, double MinY,
    double MaxX, double MaxY, double WalkableFloorAngleDeg)
{
    return Publish(LandscapeMCP::WalkabilityRegion(LandscapePath, MinX, MinY, MaxX, MaxY, WalkableFloorAngleDeg));
}
