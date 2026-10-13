#include "LandscapeMCPToolset.h"
#include "LandscapeMCPOperations.h"
#include "Kismet/KismetSystemLibrary.h"
#include "LandscapeMCPTerrainPlanning.h"
#include "LandscapeMCPApply.h"

namespace
{
    template <typename TResult>
    TResult Publish(TResult Result)
    {
        if (!Result.bSuccess) { UKismetSystemLibrary::RaiseScriptError(TEXT("LandscapeMCP: ") + Result.Message); }
        return Result;
    }
}

namespace
{
    // adapterは変更しない。失敗時はprofile配列を含めないbounded診断だけを公開する。
    // stageはExecuteが報告した処理段階であり、message文字列からは決めない。
    FLandscapeMCPTerrainOperationResult RunTerrainOperation(const LandscapeMCP::Authoring::FPreviewRequest& Request)
    {
        FString Stage;
        FLandscapeMCPTerrainOperationResult Result=LandscapeMCP::Authoring::Execute(Request,&Stage);
        if(!Result.bSuccess)
        { UKismetSystemLibrary::RaiseScriptError(TEXT("LandscapeMCP: ")+LandscapeMCP::Authoring::FormatFailureDiagnostic(Result,Stage)); }
        return Result;
    }
}

FLandscapeMCPTerrainOperationResult ULandscapeMCPToolset::FlattenPath(const FString& LandscapePath,const TArray<FVector2D>& Points,
    double WidthCm,double FalloffCm,const FString& HeightMode,double TargetHeightCm,double MaxDeltaCm,bool bDryRun)
{
    LandscapeMCP::Authoring::FPreviewRequest R;R.LandscapePath=LandscapePath;R.Points=Points;R.Width=WidthCm;
    R.Falloff=FalloffCm;R.HeightMode=HeightMode;R.TargetHeight=TargetHeightCm;R.MaxDelta=MaxDeltaCm;R.bDryRun=bDryRun;
    return RunTerrainOperation(R);
}

FLandscapeMCPTerrainOperationResult ULandscapeMCPToolset::FlattenRectangle(const FString& LandscapePath,FVector2D Center,
    double SizeXCm,double SizeYCm,double TargetHeightCm,double FalloffCm,double MaxDeltaCm,double YawDegrees,bool bDryRun)
{
    LandscapeMCP::Authoring::FPreviewRequest R;R.Operation=LandscapeMCP::Authoring::EAuthoringOperation::FlattenRectangle;
    R.LandscapePath=LandscapePath;R.Center=Center;R.SizeX=SizeXCm;R.SizeY=SizeYCm;R.TargetHeight=TargetHeightCm;
    R.Falloff=FalloffCm;R.MaxDelta=MaxDeltaCm;R.Yaw=YawDegrees;R.bDryRun=bDryRun;
    return RunTerrainOperation(R);
}

FLandscapeMCPTerrainOperationResult ULandscapeMCPToolset::CreateWalkablePath(const FString& LandscapePath,const TArray<FVector2D>& Points,
    double WidthCm,double FalloffCm,double MaxSlopeDegrees,double MaxDeltaCm,bool bDryRun)
{
    LandscapeMCP::Authoring::FPreviewRequest R;R.Operation=LandscapeMCP::Authoring::EAuthoringOperation::CreateWalkablePath;
    R.LandscapePath=LandscapePath;R.Points=Points;R.Width=WidthCm;R.Falloff=FalloffCm;R.MaxSlope=MaxSlopeDegrees;R.MaxDelta=MaxDeltaCm;R.bDryRun=bDryRun;
    return RunTerrainOperation(R);
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
