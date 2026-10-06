#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
#include "LandscapeMCPOperations.h"
#include "LandscapeMCPTerrainAnalysis.h"
#include "LandscapeMCPToolset.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Landscape.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeInfo.h"
#include "Tests/AutomationEditorCommon.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "ToolsetRegistry/ToolsetLibrary.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"

namespace
{
    // 試験用fixture。Heightfield全体を平面 Encoded = 32768 + A*X + B*Y へ置き換える。
    // Scale(100,100,100)では1格子あたりの高さ差がA*100/128 cmとなり、勾配はA/128。
    bool SetPlaneFixture(ALandscape* Actor, int32 A, int32 B)
    {
        ULandscapeInfo* Info = Actor ? Actor->GetLandscapeInfo() : nullptr;
        FIntRect Extent;
        if (!Info || !Info->GetLandscapeExtent(Extent) || !Actor->GetEditLayer(0)) { return false; }
        const FGuid LayerGuid = Actor->GetEditLayer(0)->GetGuid();
        TArray<uint16> Data; Data.Reserve((Extent.Width()+1)*(Extent.Height()+1));
        for (int32 Y = Extent.Min.Y; Y <= Extent.Max.Y; ++Y)
            for (int32 X = Extent.Min.X; X <= Extent.Max.X; ++X)
            {
                const int32 Encoded = 32768 + A*X + B*Y;
                if (Encoded < 0 || Encoded > 65535) { return false; }
                Data.Add(uint16(Encoded));
            }
        {
            FScopedSetLandscapeEditingLayer LayerScope(Actor,LayerGuid);
            FLandscapeEditDataInterface EditData(Info,LayerGuid);
            EditData.SetHeightData(Extent.Min.X,Extent.Min.Y,Extent.Max.X,Extent.Max.Y,Data.GetData(),0,false,
                nullptr,nullptr,nullptr,false,nullptr,nullptr,true,true,true);
            EditData.Flush();
        }
        Actor->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Heightmap_All);
        Actor->ForceUpdateLayersContent();
        return true;
    }
    double Degrees(double Gradient) { return FMath::RadiansToDegrees(FMath::Atan(Gradient)); }

    // 仕様どおりの上限。実装とは独立に計算する。
    double ExpectedUncertainty(double GradientX, double GradientY, double QuantizationCm, double DistanceCm)
    {
        const double E = FMath::Sqrt(2.0)*QuantizationCm/(2.0*DistanceCm);
        const double G = FMath::Sqrt(GradientX*GradientX + GradientY*GradientY);
        return FMath::Max(Degrees(G+E)-Degrees(G), Degrees(G)-Degrees(FMath::Max(0.0,G-E)));
    }
    // レビューで否定された一次近似。上限にならないことを示す比較用。
    double FirstOrderUncertainty(double GradientX, double GradientY, double QuantizationCm, double DistanceCm)
    {
        const double E = FMath::Sqrt(2.0)*QuantizationCm/(2.0*DistanceCm);
        return FMath::RadiansToDegrees(E/(1.0 + GradientX*GradientX + GradientY*GradientY));
    }
    // 4点の高さがそれぞれ+-QuantizationCm/2ずれた場合、軸ごとの勾配は-q/(2d)、0、+q/(2d)のいずれかだけ変わる。
    // 全9通りで傾斜角のずれの最大値を求める。
    double WorstQuantizedDeviation(double GradientX, double GradientY, double QuantizationCm, double DistanceCm)
    {
        const double Step = QuantizationCm/(2.0*DistanceCm);
        const double Nominal = LandscapeMCP::Analysis::SlopeFromGradient(GradientX,GradientY).SlopeDegrees;
        double Worst = 0;
        for (int32 SX = -1; SX <= 1; ++SX)
            for (int32 SY = -1; SY <= 1; ++SY)
            {
                const double Perturbed = LandscapeMCP::Analysis::SlopeFromGradient(GradientX+SX*Step,GradientY+SY*Step).SlopeDegrees;
                Worst = FMath::Max(Worst,FMath::Abs(Perturbed-Nominal));
            }
        return Worst;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeMCPTerrainAnalysisTest,"LandscapeMCP.V02.TerrainAnalysis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLandscapeMCPTerrainAnalysisTest::RunTest(const FString& Parameters)
{
    using namespace LandscapeMCP;
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    const double Infinity = std::numeric_limits<double>::infinity();
    // 平面fixtureは量子化誤差なしで表現できるため、許容差は浮動小数点誤差だけを見込む。
    const double Tolerance = 1.e-6;

    // --- Landscapeに依存しない計算層 ---
    {
        const Analysis::FSlope Flat = Analysis::SlopeFromHeights(10,10,10,10,50);
        TestEqual(TEXT("analysis flat slope"),Flat.SlopeDegrees,0.0,Tolerance);
        TestFalse(TEXT("analysis flat has no direction"),Flat.bDirectionValid);
        TestTrue(TEXT("analysis flat normal is up"),Flat.Normal.Equals(FVector::UpVector,Tolerance));
        const Analysis::FSlope PlusX = Analysis::SlopeFromHeights(0,100,0,0,50);
        TestEqual(TEXT("analysis 45 degree slope"),PlusX.SlopeDegrees,45.0,Tolerance);
        TestEqual(TEXT("analysis uphill +X"),PlusX.DirectionDegrees,0.0,Tolerance);
        TestEqual(TEXT("analysis uphill -X"),Analysis::SlopeFromHeights(100,0,0,0,50).DirectionDegrees,180.0,Tolerance);
        TestEqual(TEXT("analysis uphill +Y"),Analysis::SlopeFromHeights(0,0,0,100,50).DirectionDegrees,90.0,Tolerance);
        TestEqual(TEXT("analysis uphill -Y"),Analysis::SlopeFromHeights(0,0,100,0,50).DirectionDegrees,270.0,Tolerance);
        TestTrue(TEXT("analysis normal leans downhill"),PlusX.Normal.Equals(FVector(-1,0,1).GetSafeNormal(),Tolerance));
        const Analysis::FWalkability Exact = Analysis::EvaluateWalkability(45,45,0);
        TestTrue(TEXT("analysis exact threshold is walkable"),Exact.bWalkable);
        TestTrue(TEXT("analysis exact threshold is near limit"),Exact.Classification == Analysis::EWalkability::NearLimit);
        TestTrue(TEXT("analysis clear walkable"),Analysis::EvaluateWalkability(45,46,0.5).Classification == Analysis::EWalkability::Walkable);
        TestTrue(TEXT("analysis clear unwalkable"),Analysis::EvaluateWalkability(45,44,0.5).Classification == Analysis::EWalkability::Unwalkable);
        const double Grid[] = { 0,10,20, 0,10,50 };
        const Analysis::FRegionStats Stats = Analysis::ComputeRegionStats(Grid,3,2,10);
        TestEqual(TEXT("analysis region min"),Stats.MinHeight,0.0,Tolerance);
        TestEqual(TEXT("analysis region max"),Stats.MaxHeight,50.0,Tolerance);
        TestEqual(TEXT("analysis region mean"),Stats.MeanHeight,15.0,Tolerance);
        TestEqual(TEXT("analysis region max index"),Stats.MaxIndex,5);
        TestTrue(TEXT("analysis steepest cell is the second"),Stats.bSlopeValid && Stats.MaxSlopeCellX == 1 && Stats.MaxSlopeCellY == 0);
        TestFalse(TEXT("analysis single row has no cell slope"),Analysis::ComputeRegionStats(MakeArrayView(Grid,3),3,1,10).bSlopeValid);
    }

    // --- slopeUncertaintyDegrees: 量子化による傾斜角誤差の保守的な上限であること ---
    {
        struct FCase { const TCHAR* Name; double GX, GY, Q, D; };
        const double Q100 = 100.0/128.0; // Scale.Z=100
        const FCase Cases[] = {
            { TEXT("flat, Scale.Z 100, distance 100"), 0,0,Q100,100 },
            { TEXT("flat, minimum distance"), 0,0,Q100,1 },
            { TEXT("ramp 0.5, distance 100"), 0.5,0,Q100,100 },
            { TEXT("review counterexample: Scale.Z 100, distance 1, gradient 1"), 1,0,Q100,1 },
            { TEXT("review counterexample diagonal"), FMath::Sqrt(0.5),FMath::Sqrt(0.5),Q100,1 },
            { TEXT("diagonal, distance 5"), 0.6,0.8,Q100,5 },
            { TEXT("steep 3.0, minimum distance"), 3,0,Q100,1 },
            { TEXT("steep diagonal, distance 2"), 2,2,Q100,2 },
            { TEXT("gradient smaller than error"), 0.1,0.05,Q100,1 },
            { TEXT("maximum Scale.Z, minimum distance"), 1,1,1000.0/128.0,1 },
            { TEXT("minimum Scale.Z, maximum distance"), 0.5,0.5,1.0/128.0,5000 },
        };
        for (const FCase& C : Cases)
        {
            const Analysis::FSlope Slope = Analysis::SlopeFromGradient(C.GX,C.GY);
            const double Uncertainty = Analysis::SlopeUncertaintyDegrees(Slope,C.Q,C.D);
            TestEqual(*FString::Printf(TEXT("uncertainty formula: %s"),C.Name),Uncertainty,ExpectedUncertainty(C.GX,C.GY,C.Q,C.D),1.e-9);
            TestTrue(*FString::Printf(TEXT("uncertainty bounds every quantized perturbation: %s"),C.Name),
                WorstQuantizedDeviation(C.GX,C.GY,C.Q,C.D) <= Uncertainty + 1.e-9);
            TestTrue(*FString::Printf(TEXT("uncertainty is finite and non-negative: %s"),C.Name),FMath::IsFinite(Uncertainty) && Uncertainty >= 0);
        }
        // 反例: 斜め45度、Scale.Z=100、距離1cm。量子化だけで傾斜は約24.1度まで下がり得る。
        const double GD = FMath::Sqrt(0.5);
        const double Worst = WorstQuantizedDeviation(GD,GD,Q100,1);
        const double FirstOrder = FirstOrderUncertainty(GD,GD,Q100,1);
        const double Uncertainty = Analysis::SlopeUncertaintyDegrees(Analysis::SlopeFromGradient(GD,GD),Q100,1);
        TestTrue(TEXT("first-order approximation is not an upper bound for the counterexample"),FirstOrder < Worst);
        TestTrue(TEXT("corrected uncertainty covers the counterexample"),Uncertainty >= Worst - 1.e-9);
        // 閾値反転: 歩行可能角27度。公称45度だが量子化だけで27度未満になり得るため、UNWALKABLEと断定してはならない。
        const double Angle = 27.0;
        TestTrue(TEXT("counterexample can flip below the threshold"),45.0 - Worst < Angle);
        TestTrue(TEXT("first-order width would have missed the flip"),FMath::Abs(Angle-45.0) > FirstOrder);
        const Analysis::FWalkability Flip = Analysis::EvaluateWalkability(45.0,Angle,Uncertainty);
        TestTrue(TEXT("flip case is classified NEAR_LIMIT"),Flip.Classification == Analysis::EWalkability::NearLimit);
        TestFalse(TEXT("flip case keeps nominal bWalkable"),Flip.bWalkable);
        // 平坦側: 公称0度でも量子化だけで約28.9度まで上がり得る。歩行可能角20度はNEAR_LIMIT、40度はWALKABLE。
        const double FlatUncertainty = Analysis::SlopeUncertaintyDegrees(Analysis::SlopeFromGradient(0,0),Q100,1);
        TestEqual(TEXT("flat uncertainty at minimum distance"),FlatUncertainty,Degrees(FMath::Sqrt(2.0)*Q100/2.0),1.e-9);
        TestTrue(TEXT("flat flip case is NEAR_LIMIT"),Analysis::EvaluateWalkability(0,20,FlatUncertainty).Classification == Analysis::EWalkability::NearLimit);
        TestTrue(TEXT("flat clear case is WALKABLE"),Analysis::EvaluateWalkability(0,40,FlatUncertainty).Classification == Analysis::EWalkability::Walkable);
    }

    // --- 隔離した未保存LevelでのLandscape読み取り ---
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    if (!TestNotNull(TEXT("isolated unsaved test world"),World)) { return false; }
    const FString LevelPath = World->PersistentLevel->GetPathName();
    int32 SaveEvents = 0;
    const FDelegateHandle SaveHandle = FCoreUObjectDelegates::OnObjectPreSave.AddLambda(
        [&](UObject* Object,FObjectPreSaveContext Context)
        { if (Object && Object->GetOutermost() == World->GetOutermost()) { ++SaveEvents; } });
    struct FSaveGuard
    {
        FDelegateHandle Handle;
        ~FSaveGuard() { FCoreUObjectDelegates::OnObjectPreSave.Remove(Handle); }
    } SaveGuard { SaveHandle };
    AActor* Sentinel = World->SpawnActor<AActor>();
    const FTransform SentinelBefore = Sentinel->GetActorTransform();

    // 29x29 Sample、0〜2800cm四方。
    const FLandscapeMCPResult Created = Create(LevelPath,TEXT("AI_SlopeLandscape"),FVector::ZeroVector,FVector(100,100,100),2,2,2,7,0,false);
    if (!TestTrue(*Created.Message,Created.bSuccess)) { return false; }
    const FString Path = Created.LandscapePath;
    ALandscape* Actor = FindObject<ALandscape>(nullptr,*Path);
    if (!TestNotNull(TEXT("created Landscape"),Actor)) { return false; }

    // 平坦
    FLandscapeMCPSlopeResult S = Slope(Path,1400,1400,100);
    TestTrue(*S.Message,S.bSuccess);
    TestEqual(TEXT("flat terrain slope"),S.SlopeDegrees,0.0,Tolerance);
    TestFalse(TEXT("flat terrain has no direction"),S.bDirectionValid);
    TestTrue(TEXT("flat terrain normal is up"),S.Normal.Equals(FVector::UpVector,Tolerance));
    TestEqual(TEXT("flat quantization"),S.HeightQuantizationCm,0.78125,Tolerance);
    TestTrue(TEXT("flat terrain walkable"),Walkability(Path,1400,1400,44.77,100).bWalkable);

    // 入力Validation
    TestFalse(TEXT("zero sample distance"),Slope(Path,1400,1400,0).bSuccess);
    TestFalse(TEXT("negative sample distance"),Slope(Path,1400,1400,-100).bSuccess);
    TestFalse(TEXT("too small sample distance"),Slope(Path,1400,1400,0.5).bSuccess);
    TestFalse(TEXT("oversized sample distance"),Slope(Path,1400,1400,5001).bSuccess);
    TestFalse(TEXT("NaN sample distance"),Slope(Path,1400,1400,NaN).bSuccess);
    TestFalse(TEXT("infinite sample distance"),Slope(Path,1400,1400,Infinity).bSuccess);
    TestFalse(TEXT("NaN X"),Slope(Path,NaN,1400,100).bSuccess);
    TestFalse(TEXT("infinite Y"),Slope(Path,1400,Infinity,100).bSuccess);
    TestFalse(TEXT("name instead of path"),Slope(TEXT("AI_SlopeLandscape"),1400,1400,100).bSuccess);
    TestFalse(TEXT("missing exact path"),Slope(LevelPath+TEXT(".Missing"),1400,1400,100).bSuccess);

    // 端はclipせず拒否する
    TestFalse(TEXT("stencil crosses minimum edge"),Slope(Path,50,1400,100).bSuccess);
    TestFalse(TEXT("stencil crosses maximum edge"),Slope(Path,2800,1400,100).bSuccess);
    TestFalse(TEXT("stencil crosses Y edge"),Slope(Path,1400,2750,100).bSuccess);
    TestFalse(TEXT("centre outside"),Slope(Path,2900,1400,100).bSuccess);
    TestTrue(TEXT("stencil exactly reaching edge is measurable"),Slope(Path,100,1400,100).bSuccess);
    TestFalse(TEXT("walkability stencil crosses edge"),Walkability(Path,50,1400,45,100).bSuccess);

    // 既知の斜面: +Xへ1格子あたり64(=50cm/100cm)。勾配0.5。
    if (!TestTrue(TEXT("ramp fixture"),SetPlaneFixture(Actor,64,0))) { return false; }
    const double Ramp = Degrees(0.5);
    S = Slope(Path,1400,1400,100);
    TestTrue(*S.Message,S.bSuccess);
    TestEqual(TEXT("known ramp slope"),S.SlopeDegrees,Ramp,Tolerance);
    TestTrue(TEXT("ramp direction valid"),S.bDirectionValid);
    TestEqual(TEXT("ramp uphill is +X"),S.SlopeDirectionDegrees,0.0,Tolerance);
    TestTrue(TEXT("ramp normal"),S.Normal.Equals(FVector(-0.5,0,1).GetSafeNormal(),Tolerance));
    TestEqual(TEXT("ramp centre height"),S.HeightCenterCm,700.0,Tolerance);
    TestEqual(TEXT("centre height matches GetHeight"),S.HeightCenterCm,Height(Path,1400,1400).HeightCm,Tolerance);
    // 平面では計測位置と距離に依存しない
    TestEqual(TEXT("ramp slope off-grid with small distance"),Slope(Path,1437.5,912.25,13).SlopeDegrees,Ramp,Tolerance);
    TestEqual(TEXT("ramp slope with large distance"),Slope(Path,1400,1400,1000).SlopeDegrees,Ramp,Tolerance);
    TestTrue(TEXT("uncertainty reflects quantization"),S.SlopeUncertaintyDegrees > 0.2 && S.SlopeUncertaintyDegrees < 0.3);
    // facade経由でも同じ値
    TestEqual(TEXT("facade slope"),ULandscapeMCPToolset::GetSlope(Path,1400,1400,100).SlopeDegrees,Ramp,Tolerance);

    // GetHeightRegion: x=200..1000、y=300..700を200cm間隔 → 5x3
    FLandscapeMCPHeightRegionResult Region = HeightRegion(Path,200,300,1000,700,200);
    TestTrue(*Region.Message,Region.bSuccess);
    TestEqual(TEXT("region sample count"),Region.SampleCount,15);
    TestEqual(TEXT("region count X"),Region.SampleCountX,5);
    TestEqual(TEXT("region count Y"),Region.SampleCountY,3);
    TestEqual(TEXT("region heights array"),Region.HeightsCm.Num(),15);
    TestEqual(TEXT("region min"),Region.MinHeightCm,100.0,Tolerance);
    TestEqual(TEXT("region max"),Region.MaxHeightCm,500.0,Tolerance);
    TestEqual(TEXT("region mean"),Region.MeanHeightCm,300.0,Tolerance);
    if (Region.HeightsCm.Num() == 15)
    {
        TestEqual(TEXT("region row-major first"),Region.HeightsCm[0],100.0,Tolerance);
        TestEqual(TEXT("region row-major X step"),Region.HeightsCm[1],200.0,Tolerance);
        TestEqual(TEXT("region row-major Y step"),Region.HeightsCm[5],100.0,Tolerance);
        TestEqual(TEXT("region sample matches GetHeight"),Region.HeightsCm[7],Height(Path,600,500).HeightCm,Tolerance);
    }
    TestEqual(TEXT("region min location X"),Region.MinHeightLocation.X,200.0,Tolerance);
    TestEqual(TEXT("region max location X"),Region.MaxHeightLocation.X,1000.0,Tolerance);
    TestTrue(TEXT("region world max"),Region.WorldMax.Equals(FVector(1000,700,500),Tolerance));
    TestTrue(TEXT("region slope valid"),Region.bSlopeValid);
    TestEqual(TEXT("region max slope equals ramp"),Region.MaxSlopeDegrees,Ramp,Tolerance);
    // 割り切れない間隔は要求maxの内側で止まる
    Region = HeightRegion(Path,200,300,1050,700,200);
    TestEqual(TEXT("non-dividing spacing count"),Region.SampleCount,15);
    TestEqual(TEXT("non-dividing spacing sampled max"),Region.WorldMax.X,1000.0,Tolerance);
    // 1点・1行
    Region = HeightRegion(Path,600,500,600,500,100);
    TestTrue(TEXT("single point region"),Region.bSuccess && Region.SampleCount == 1 && !Region.bSlopeValid);
    TestEqual(TEXT("single point height"),Region.MeanHeightCm,300.0,Tolerance);
    Region = HeightRegion(Path,0,0,2800,0,100);
    TestTrue(TEXT("single row along inclusive edge"),Region.bSuccess && Region.SampleCount == 29 && !Region.bSlopeValid);
    // Sample上限: 32x32=1024は許可、34x34は拒否
    TestEqual(TEXT("region at sample limit"),HeightRegion(Path,0,0,2790,2790,90).SampleCount,1024);
    TestFalse(TEXT("oversized region rejected"),HeightRegion(Path,0,0,2800,2800,84).bSuccess);
    TestFalse(TEXT("oversized single axis rejected"),HeightRegion(Path,0,0,2800,0,1).bSuccess);
    // 異常入力
    TestFalse(TEXT("reversed X bounds"),HeightRegion(Path,1000,300,200,700,200).bSuccess);
    TestFalse(TEXT("reversed Y bounds"),HeightRegion(Path,200,700,1000,300,200).bSuccess);
    TestFalse(TEXT("region NaN bound"),HeightRegion(Path,NaN,300,1000,700,200).bSuccess);
    TestFalse(TEXT("region infinite bound"),HeightRegion(Path,200,300,Infinity,700,200).bSuccess);
    TestFalse(TEXT("region zero spacing"),HeightRegion(Path,200,300,1000,700,0).bSuccess);
    TestFalse(TEXT("region negative spacing"),HeightRegion(Path,200,300,1000,700,-200).bSuccess);
    TestFalse(TEXT("region NaN spacing"),HeightRegion(Path,200,300,1000,700,NaN).bSuccess);
    TestFalse(TEXT("region infinite spacing"),HeightRegion(Path,200,300,1000,700,Infinity).bSuccess);
    TestFalse(TEXT("region oversized spacing"),HeightRegion(Path,200,300,1000,700,5001).bSuccess);
    TestFalse(TEXT("region out of range max"),HeightRegion(Path,200,300,2900,700,200).bSuccess);
    TestFalse(TEXT("region out of range min"),HeightRegion(Path,-100,300,1000,700,200).bSuccess);
    TestFalse(TEXT("region missing path"),HeightRegion(LevelPath+TEXT(".Missing"),200,300,1000,700,200).bSuccess);

    // Walkability: 斜面26.565度
    FLandscapeMCPWalkabilityResult W = Walkability(Path,1400,1400,44.77,100);
    TestTrue(*W.Message,W.bSuccess);
    TestTrue(TEXT("below threshold is walkable"),W.bWalkable);
    TestEqual(TEXT("below threshold classification"),W.Classification,FString(TEXT("WALKABLE")));
    TestEqual(TEXT("margin"),W.MarginDegrees,44.77-Ramp,Tolerance);
    TestEqual(TEXT("walkability slope equals GetSlope"),W.SlopeDegrees,S.SlopeDegrees,Tolerance);
    W = Walkability(Path,1400,1400,20,100);
    TestTrue(TEXT("above threshold evaluated"),W.bSuccess);
    TestFalse(TEXT("above threshold is unwalkable"),W.bWalkable);
    TestEqual(TEXT("above threshold classification"),W.Classification,FString(TEXT("UNWALKABLE")));
    TestTrue(TEXT("negative margin"),W.MarginDegrees < 0);
    // 境界値: 同じ角度は歩行可能側、量子化誤差内はNEAR_LIMIT
    W = Walkability(Path,1400,1400,S.SlopeDegrees,100);
    TestTrue(TEXT("exact threshold is walkable"),W.bWalkable);
    TestEqual(TEXT("exact threshold classification"),W.Classification,FString(TEXT("NEAR_LIMIT")));
    W = Walkability(Path,1400,1400,S.SlopeDegrees-0.1,100);
    TestFalse(TEXT("just above limit is unwalkable"),W.bWalkable);
    TestEqual(TEXT("just above limit classification"),W.Classification,FString(TEXT("NEAR_LIMIT")));
    W = Walkability(Path,1400,1400,S.SlopeDegrees+0.1,100);
    TestTrue(TEXT("just below limit is walkable"),W.bWalkable);
    TestEqual(TEXT("just below limit classification"),W.Classification,FString(TEXT("NEAR_LIMIT")));
    TestEqual(TEXT("clear of limit classification"),Walkability(Path,1400,1400,S.SlopeDegrees+1,100).Classification,FString(TEXT("WALKABLE")));
    TestEqual(TEXT("angle 90 accepts ramp"),Walkability(Path,1400,1400,90,100).Classification,FString(TEXT("WALKABLE")));
    TestFalse(TEXT("angle 0 rejects ramp"),Walkability(Path,1400,1400,0,100).bWalkable);
    TestFalse(TEXT("negative angle"),Walkability(Path,1400,1400,-1,100).bSuccess);
    TestFalse(TEXT("angle above 90"),Walkability(Path,1400,1400,90.1,100).bSuccess);
    TestFalse(TEXT("NaN angle"),Walkability(Path,1400,1400,NaN,100).bSuccess);
    TestFalse(TEXT("infinite angle"),Walkability(Path,1400,1400,Infinity,100).bSuccess);
    TestFalse(TEXT("walkability invalid distance"),Walkability(Path,1400,1400,45,0).bSuccess);

    // 下り方向と斜め方向
    if (!TestTrue(TEXT("downhill fixture"),SetPlaneFixture(Actor,-64,0))) { return false; }
    S = Slope(Path,1400,1400,100);
    TestEqual(TEXT("downhill +X means uphill -X"),S.SlopeDirectionDegrees,180.0,Tolerance);
    TestEqual(TEXT("downhill slope magnitude"),S.SlopeDegrees,Ramp,Tolerance);
    if (!TestTrue(TEXT("Y ramp fixture"),SetPlaneFixture(Actor,0,64))) { return false; }
    TestEqual(TEXT("uphill +Y"),Slope(Path,1400,1400,100).SlopeDirectionDegrees,90.0,Tolerance);
    if (!TestTrue(TEXT("diagonal fixture"),SetPlaneFixture(Actor,64,128))) { return false; }
    S = Slope(Path,1400,1400,100);
    TestEqual(TEXT("2D gradient slope"),S.SlopeDegrees,Degrees(FMath::Sqrt(1.25)),Tolerance);
    TestEqual(TEXT("2D gradient direction"),S.SlopeDirectionDegrees,FMath::RadiansToDegrees(FMath::Atan2(1.0,0.5)),Tolerance);

    // 実地検証で前進を阻止された約47度の斜面を事前に判定できること
    if (!TestTrue(TEXT("steep fixture"),SetPlaneFixture(Actor,137,0))) { return false; }
    W = Walkability(Path,1400,1400,44.77,100);
    TestTrue(TEXT("steep slope evaluated"),W.bSuccess);
    TestEqual(TEXT("steep slope angle"),W.SlopeDegrees,Degrees(137.0/128.0),Tolerance);
    TestFalse(TEXT("47 degree slope is unwalkable at 44.77"),W.bWalkable);
    TestEqual(TEXT("steep classification"),W.Classification,FString(TEXT("UNWALKABLE")));

    // 最小distance: 平面なので傾斜は同じ、不確かさだけが広がる
    S = Slope(Path,1400,1400,1);
    TestTrue(*S.Message,S.bSuccess);
    TestEqual(TEXT("minimum distance slope"),S.SlopeDegrees,Degrees(137.0/128.0),Tolerance);
    TestEqual(TEXT("minimum distance uncertainty"),S.SlopeUncertaintyDegrees,ExpectedUncertainty(137.0/128.0,0,0.78125,1),Tolerance);
    TestTrue(TEXT("minimum distance uncertainty bounds perturbations"),WorstQuantizedDeviation(137.0/128.0,0,0.78125,1) <= S.SlopeUncertaintyDegrees + Tolerance);
    // 約46.9度を歩行可能角40度で評価。距離100では明確にUNWALKABLE、距離1では量子化で反転し得るためNEAR_LIMIT。
    TestEqual(TEXT("steep at distance 100 is UNWALKABLE"),Walkability(Path,1400,1400,40,100).Classification,FString(TEXT("UNWALKABLE")));
    W = Walkability(Path,1400,1400,40,1);
    TestEqual(TEXT("steep at minimum distance is NEAR_LIMIT"),W.Classification,FString(TEXT("NEAR_LIMIT")));
    TestFalse(TEXT("steep at minimum distance keeps nominal bWalkable"),W.bWalkable);
    TestEqual(TEXT("walkability uncertainty equals GetSlope"),W.SlopeUncertaintyDegrees,S.SlopeUncertaintyDegrees,Tolerance);

    // レビューの反例: Scale(1,1,100)、distance 1cm。量子化刻み0.78125cmに対して水平距離が1cmしかない。
    {
        const FLandscapeMCPResult Fine = Create(LevelPath,TEXT("AI_FineLandscape"),FVector(5000,0,0),FVector(1,1,100),1,1,1,7,0,false);
        if (!TestTrue(*Fine.Message,Fine.bSuccess)) { return false; }
        ALandscape* FineActor = FindObject<ALandscape>(nullptr,*Fine.LandscapePath);
        if (!TestNotNull(TEXT("fine Landscape"),FineActor)) { return false; }
        // 平坦: 公称0度でも不確かさは約28.9度
        FLandscapeMCPSlopeResult F = Slope(Fine.LandscapePath,5003.5,3.5,1);
        TestTrue(*F.Message,F.bSuccess);
        TestEqual(TEXT("fine flat slope"),F.SlopeDegrees,0.0,Tolerance);
        TestEqual(TEXT("fine flat uncertainty"),F.SlopeUncertaintyDegrees,ExpectedUncertainty(0,0,0.78125,1),Tolerance);
        TestEqual(TEXT("fine flat near threshold is NEAR_LIMIT"),Walkability(Fine.LandscapePath,5003.5,3.5,20,1).Classification,FString(TEXT("NEAR_LIMIT")));
        TestEqual(TEXT("fine flat clear of threshold is WALKABLE"),Walkability(Fine.LandscapePath,5003.5,3.5,40,1).Classification,FString(TEXT("WALKABLE")));
        // 斜め: 1格子(1cm)あたり1単位(0.78125cm)ずつ。勾配(0.78125,0.78125)、約47.85度。
        if (!TestTrue(TEXT("fine diagonal fixture"),SetPlaneFixture(FineActor,1,1))) { return false; }
        const double G = 0.78125;
        F = Slope(Fine.LandscapePath,5003.5,3.5,1);
        TestTrue(*F.Message,F.bSuccess);
        TestEqual(TEXT("fine diagonal slope"),F.SlopeDegrees,Degrees(G*FMath::Sqrt(2.0)),Tolerance);
        TestEqual(TEXT("fine diagonal direction"),F.SlopeDirectionDegrees,45.0,Tolerance);
        TestEqual(TEXT("fine diagonal uncertainty"),F.SlopeUncertaintyDegrees,ExpectedUncertainty(G,G,0.78125,1),Tolerance);
        const double FineWorst = WorstQuantizedDeviation(G,G,0.78125,1);
        TestTrue(TEXT("fine diagonal uncertainty bounds perturbations"),FineWorst <= F.SlopeUncertaintyDegrees + Tolerance);
        TestTrue(TEXT("fine diagonal: first-order approximation was not a bound"),FirstOrderUncertainty(G,G,0.78125,1) < FineWorst);
        // 歩行可能角30度: 公称は約17.9度超過だが、量子化だけで約28.9度まで下がり得る
        const FLandscapeMCPWalkabilityResult FW = Walkability(Fine.LandscapePath,5003.5,3.5,30,1);
        TestTrue(*FW.Message,FW.bSuccess);
        TestTrue(TEXT("fine diagonal margin exceeds first-order width"),FMath::Abs(FW.MarginDegrees) > FirstOrderUncertainty(G,G,0.78125,1));
        TestTrue(TEXT("fine diagonal can flip below the threshold"),FW.SlopeDegrees - FineWorst < 30.0);
        TestEqual(TEXT("fine diagonal threshold flip is NEAR_LIMIT"),FW.Classification,FString(TEXT("NEAR_LIMIT")));
        TestFalse(TEXT("fine diagonal keeps nominal bWalkable"),FW.bWalkable);
        TestEqual(TEXT("fine diagonal far above threshold is UNWALKABLE"),Walkability(Fine.LandscapePath,5003.5,3.5,10,1).Classification,FString(TEXT("UNWALKABLE")));
        TestEqual(TEXT("fine diagonal far below threshold is WALKABLE"),Walkability(Fine.LandscapePath,5003.5,3.5,80,1).Classification,FString(TEXT("WALKABLE")));
    }

    // 非等方Scale(200,50,25): 1格子あたり64単位(12.5cm)。勾配はX方向0.0625、Y方向0.25。
    {
        const FLandscapeMCPResult Aniso = Create(LevelPath,TEXT("AI_AnisoLandscape"),FVector(8000,0,0),FVector(200,50,25),1,1,1,7,0,false);
        if (!TestTrue(*Aniso.Message,Aniso.bSuccess)) { return false; }
        ALandscape* AnisoActor = FindObject<ALandscape>(nullptr,*Aniso.LandscapePath);
        if (!TestNotNull(TEXT("anisotropic Landscape"),AnisoActor)) { return false; }
        if (!TestTrue(TEXT("anisotropic fixture"),SetPlaneFixture(AnisoActor,64,64))) { return false; }
        const double GX = 12.5/200.0, GY = 12.5/50.0, Q = 25.0/128.0;
        const FLandscapeMCPSlopeResult A = Slope(Aniso.LandscapePath,8700,175,50);
        TestTrue(*A.Message,A.bSuccess);
        TestEqual(TEXT("anisotropic quantization"),A.HeightQuantizationCm,Q,Tolerance);
        TestEqual(TEXT("anisotropic slope"),A.SlopeDegrees,Degrees(FMath::Sqrt(GX*GX+GY*GY)),Tolerance);
        TestEqual(TEXT("anisotropic direction"),A.SlopeDirectionDegrees,FMath::RadiansToDegrees(FMath::Atan2(GY,GX)),Tolerance);
        TestEqual(TEXT("anisotropic uncertainty"),A.SlopeUncertaintyDegrees,ExpectedUncertainty(GX,GY,Q,50),Tolerance);
        TestTrue(TEXT("anisotropic uncertainty bounds perturbations"),WorstQuantizedDeviation(GX,GY,Q,50) <= A.SlopeUncertaintyDegrees + Tolerance);
        const FLandscapeMCPSlopeResult AMin = Slope(Aniso.LandscapePath,8700,175,1);
        TestEqual(TEXT("anisotropic minimum distance uncertainty"),AMin.SlopeUncertaintyDegrees,ExpectedUncertainty(GX,GY,Q,1),Tolerance);
        TestTrue(TEXT("smaller distance widens uncertainty"),AMin.SlopeUncertaintyDegrees > A.SlopeUncertaintyDegrees);
    }

    // 読み取り専用: Package、Undo履歴、他Actorを変更しない
    World->GetOutermost()->SetDirtyFlag(false);
    const int32 UndoBefore = UToolsetLibrary::GetActiveUndoCount();
    TestTrue(TEXT("read slope"),Slope(Path,1400,1400,100).bSuccess);
    TestTrue(TEXT("read region"),HeightRegion(Path,0,0,2800,2800,100).bSuccess);
    TestTrue(TEXT("read walkability"),Walkability(Path,1400,1400,44.77,100).bSuccess);
    TestFalse(TEXT("failed read"),Slope(Path,50,1400,100).bSuccess);
    TestFalse(TEXT("read-only operations do not dirty package"),World->GetOutermost()->IsDirty());
    TestEqual(TEXT("read-only operations do not create Undo entries"),UToolsetLibrary::GetActiveUndoCount(),UndoBefore);
    TestTrue(TEXT("sentinel Actor unchanged"),Sentinel->GetActorTransform().Equals(SentinelBefore));
    TestEqual(TEXT("no Level / Asset save events"),SaveEvents,0);

    // v0.1のSafety境界を共有する
    ULandscapeEditLayerBase* Layer = Actor->GetEditLayer(0);
    Layer->SetLocked(true,false);
    TestFalse(TEXT("locked layer refuses slope"),Slope(Path,1400,1400,100).bSuccess);
    TestFalse(TEXT("locked layer refuses region"),HeightRegion(Path,200,300,1000,700,200).bSuccess);
    TestFalse(TEXT("locked layer refuses walkability"),Walkability(Path,1400,1400,45,100).bSuccess);
    Layer->SetLocked(false,false);
    Actor->SetActorRotation(FRotator(0,10,0));
    TestFalse(TEXT("rotated Landscape refuses slope"),Slope(Path,1400,1400,100).bSuccess);
    Actor->SetActorRotation(FRotator::ZeroRotator);
    TestTrue(TEXT("slope available again"),Slope(Path,1400,1400,100).bSuccess);

    // schema: 既存5 Toolを保持し、新3 Toolを公開する
    const FString Schema = UToolsetRegistry::GetToolsetJsonSchema(ULandscapeMCPToolset::StaticClass());
    for (const TCHAR* Tool : { TEXT("CreateLandscape"),TEXT("GetHeight"),TEXT("SculptRegion"),TEXT("SmoothRegion"),TEXT("FlattenRegion"),
        TEXT("GetSlope"),TEXT("GetHeightRegion"),TEXT("EvaluateWalkability") })
    { TestTrue(*FString::Printf(TEXT("official schema includes %s"),Tool),Schema.Contains(Tool)); }
    // Versionの完全一致は最新のsuite(V03)で確認する。ここではv0.2以降であることだけを見る。
    TestTrue(TEXT("toolset version is 0.2 or later"),FCString::Atod(*GetDefault<ULandscapeMCPToolset>()->GetToolsetVersion()) >= 0.2 - 1.e-9);
    AddInfo(Schema);
    return true;
}
#endif
