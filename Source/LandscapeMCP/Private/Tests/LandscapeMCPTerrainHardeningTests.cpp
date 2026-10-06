#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
#include "LandscapeMCPOperations.h"
#include "LandscapeMCPTerrainAnalysis.h"
#include "LandscapeMCPToolset.h"
#include "CollisionQueryParams.h"
#include "Editor.h"
#include "Engine/HitResult.h"
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

namespace LandscapeMCPHardeningTest
{
    // 試験用fixture。Heightfield全体を Encoded = 32768 + Units(X,Y) へ置き換える。
    // Scale.Z=100では128単位が100cm。
    bool SetHeightFixture(ALandscape* Actor, TFunctionRef<int32(int32,int32)> Units)
    {
        ULandscapeInfo* Info = Actor ? Actor->GetLandscapeInfo() : nullptr;
        FIntRect Extent;
        if (!Info || !Info->GetLandscapeExtent(Extent) || !Actor->GetEditLayer(0)) { return false; }
        const FGuid LayerGuid = Actor->GetEditLayer(0)->GetGuid();
        TArray<uint16> Data; Data.Reserve((Extent.Width()+1)*(Extent.Height()+1));
        for (int32 Y = Extent.Min.Y; Y <= Extent.Max.Y; ++Y)
            for (int32 X = Extent.Min.X; X <= Extent.Max.X; ++X)
            {
                const int32 Encoded = 32768 + Units(X,Y);
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
    double SlopeAngleOf(double Gradient) { return FMath::RadiansToDegrees(FMath::Atan(Gradient)); }

    // 実装とは独立に、GetHeightで読んだ頂点から半径内の実三角形(対角線00-11)を集計する。
    // Scale(100,100,*)・原点(0,0)のLandscape専用。
    struct FExpectedLocal { double Max = 0; double Mean = 0; int32 Count = 0; };
    FExpectedLocal ExpectedLocal(const FString& Path, double X, double Y, double Radius, int32 MaxVertex)
    {
        FExpectedLocal E; double Sum = 0;
        const int32 CenterX = FMath::Clamp(FMath::FloorToInt32(X/100.0),0,MaxVertex-1);
        const int32 CenterY = FMath::Clamp(FMath::FloorToInt32(Y/100.0),0,MaxVertex-1);
        for (int32 CY = 0; CY < MaxVertex; ++CY)
            for (int32 CX = 0; CX < MaxVertex; ++CX)
            {
                const double H00 = LandscapeMCP::Height(Path,CX*100.0,CY*100.0).HeightCm, H10 = LandscapeMCP::Height(Path,(CX+1)*100.0,CY*100.0).HeightCm;
                const double H01 = LandscapeMCP::Height(Path,CX*100.0,(CY+1)*100.0).HeightCm, H11 = LandscapeMCP::Height(Path,(CX+1)*100.0,(CY+1)*100.0).HeightCm;
                const double Slopes[2] = { SlopeAngleOf(FMath::Sqrt(FMath::Square((H10-H00)/100.0)+FMath::Square((H11-H10)/100.0))),
                    SlopeAngleOf(FMath::Sqrt(FMath::Square((H11-H01)/100.0)+FMath::Square((H01-H00)/100.0))) };
                const FVector2D Centroids[2] = { FVector2D((CX+2.0/3.0)*100.0,(CY+1.0/3.0)*100.0), FVector2D((CX+1.0/3.0)*100.0,(CY+2.0/3.0)*100.0) };
                for (int32 Index = 0; Index < 2; ++Index)
                {
                    if (!(CX == CenterX && CY == CenterY) && FVector2D::Distance(Centroids[Index],FVector2D(X,Y)) > Radius) { continue; }
                    E.Max = FMath::Max(E.Max,Slopes[Index]); Sum += Slopes[Index]; ++E.Count;
                }
            }
        E.Mean = E.Count > 0 ? Sum/E.Count : 0;
        return E;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeMCPTerrainHardeningTest,"LandscapeMCP.V03.TerrainHardening",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLandscapeMCPTerrainHardeningTest::RunTest(const FString& Parameters)
{
    using namespace LandscapeMCP;
    using namespace LandscapeMCPHardeningTest;
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    const double Infinity = std::numeric_limits<double>::infinity();
    const double Tolerance = 1.e-6;
    const double Diagonal = SlopeAngleOf(FMath::Sqrt(2.0)); // 勾配(1,-1)の三角形。約54.7356度。
    const auto All = [](int32,int32,bool) { return true; };

    // --- 計算層: 三角形分割 ---
    {
        // 4隅[00,10,01,11]=[0,100,100,0]。4隅平均の勾配は0だが、実三角形は2枚とも急斜面。
        const Analysis::FCellTriangles Twisted = Analysis::CellTriangleSlopes(0,100,100,0,100,100);
        TestEqual(TEXT("twisted cell lower triangle"),Twisted.Lower.SlopeDegrees,Diagonal,Tolerance);
        TestEqual(TEXT("twisted cell upper triangle"),Twisted.Upper.SlopeDegrees,Diagonal,Tolerance);
        const double TwistedGrid[] = { 0,100, 100,0 };
        const Analysis::FRegionStats CellAverage = Analysis::ComputeRegionStats(TwistedGrid,2,2,100);
        TestEqual(TEXT("twisted cell average gradient stays 0 (v0.2 definition unchanged)"),CellAverage.MaxSlopeDegrees,0.0,Tolerance);
        const Analysis::FTriangleStats TwistedStats = Analysis::ComputeTriangleStats(TwistedGrid,2,2,100,100,90,0,All);
        TestEqual(TEXT("twisted cell triangle count"),TwistedStats.TriangleCount,2);
        TestEqual(TEXT("twisted cell max triangle slope"),TwistedStats.MaxSlopeDegrees,Diagonal,Tolerance);
        // 対角線の向きを固定する: 10だけ高いセルは、対角線00-11ならLowerだけが傾き、Upperは水平。
        const Analysis::FCellTriangles Corner10 = Analysis::CellTriangleSlopes(0,100,0,0,100,100);
        TestEqual(TEXT("diagonal 00-11: lower triangle carries vertex 10"),Corner10.Lower.SlopeDegrees,Diagonal,Tolerance);
        TestEqual(TEXT("diagonal 00-11: upper triangle is flat"),Corner10.Upper.SlopeDegrees,0.0,Tolerance);
        const Analysis::FCellTriangles Corner01 = Analysis::CellTriangleSlopes(0,0,100,0,100,100);
        TestEqual(TEXT("diagonal 00-11: lower triangle is flat"),Corner01.Lower.SlopeDegrees,0.0,Tolerance);
        TestEqual(TEXT("diagonal 00-11: upper triangle carries vertex 01"),Corner01.Upper.SlopeDegrees,Diagonal,Tolerance);
        // 平面では2枚とも同じ傾斜・方向。非等方な間隔も反映する。
        const Analysis::FCellTriangles Plane = Analysis::CellTriangleSlopes(0,50,25,75,200,50);
        TestEqual(TEXT("plane lower slope"),Plane.Lower.SlopeDegrees,SlopeAngleOf(FMath::Sqrt(0.25*0.25+0.5*0.5)),Tolerance);
        TestEqual(TEXT("plane upper equals lower"),Plane.Upper.SlopeDegrees,Plane.Lower.SlopeDegrees,Tolerance);
        TestEqual(TEXT("plane direction"),Plane.Lower.DirectionDegrees,FMath::RadiansToDegrees(FMath::Atan2(0.5,0.25)),Tolerance);
        TestTrue(TEXT("lower centroid"),Analysis::TriangleCentroid(3,5,false).Equals(FVector2D(3+2.0/3.0,5+1.0/3.0),Tolerance));
        TestTrue(TEXT("upper centroid"),Analysis::TriangleCentroid(3,5,true).Equals(FVector2D(3+1.0/3.0,5+2.0/3.0),Tolerance));
        // 集計: 3x2頂点 = 2セル = 4三角形。除外と分類。
        const double Mixed[] = { 0,0,100, 0,0,100 }; // 左セルは水平、右セルは45度
        const Analysis::FTriangleStats MixedStats = Analysis::ComputeTriangleStats(Mixed,3,2,100,100,30,0,All);
        TestEqual(TEXT("mixed triangle count"),MixedStats.TriangleCount,4);
        TestEqual(TEXT("mixed walkable count"),MixedStats.WalkableCount,2);
        TestEqual(TEXT("mixed unwalkable count"),MixedStats.UnwalkableCount,2);
        TestEqual(TEXT("mixed mean slope"),MixedStats.MeanSlopeDegrees,22.5,Tolerance);
        TestEqual(TEXT("mixed max cell"),MixedStats.MaxCellX,1);
        TestTrue(TEXT("mixed region classification"),Analysis::ClassifyRegion(MixedStats) == Analysis::ERegionWalkability::Mixed);
        const Analysis::FTriangleStats LeftOnly = Analysis::ComputeTriangleStats(Mixed,3,2,100,100,30,0,[](int32 X,int32,bool) { return X == 0; });
        TestEqual(TEXT("filtered triangle count"),LeftOnly.TriangleCount,2);
        TestTrue(TEXT("all walkable region"),Analysis::ClassifyRegion(LeftOnly) == Analysis::ERegionWalkability::Walkable);
        const Analysis::FTriangleStats RightOnly = Analysis::ComputeTriangleStats(Mixed,3,2,100,100,30,0,[](int32 X,int32,bool) { return X == 1; });
        TestTrue(TEXT("all unwalkable region"),Analysis::ClassifyRegion(RightOnly) == Analysis::ERegionWalkability::Unwalkable);
        // 境界値は量子化誤差0でもNEAR_LIMITとなり、領域はMIXED。
        const Analysis::FTriangleStats Exact = Analysis::ComputeTriangleStats(Mixed,3,2,100,100,45,0,[](int32 X,int32,bool) { return X == 1; });
        TestEqual(TEXT("exact threshold triangles are near limit"),Exact.NearLimitCount,2);
        TestTrue(TEXT("near limit region is mixed"),Analysis::ClassifyRegion(Exact) == Analysis::ERegionWalkability::Mixed);
        TestEqual(TEXT("empty grid"),Analysis::ComputeTriangleStats(MakeArrayView(Mixed,3),3,1,100,100,30,0,All).TriangleCount,0);
        TestEqual(TEXT("triangle gradient error"),Analysis::TriangleGradientError(0.78125,100,100),FMath::Sqrt(2.0)*0.0078125,1.e-12);
        TestEqual(TEXT("anisotropic triangle gradient error"),Analysis::TriangleGradientError(1,200,50),FMath::Sqrt(0.005*0.005+0.02*0.02),1.e-12);
    }

    // --- 計算層: slopeUncertaintyDegreesの連続誤差領域 ---
    // 真の勾配は、測定値を中心とする半径eの円板内にある。その中の勾配の大きさは[max(0,g-e), g+e]。
    {
        struct FCase { const TCHAR* Name; double GX, GY, Q, D; };
        const double Q100 = 100.0/128.0;
        const FCase Cases[] = {
            { TEXT("g < e, axis"), 0.1,0,Q100,1 },
            { TEXT("g < e, diagonal"), 0.2,0.3,Q100,1 },
            { TEXT("g slightly below e"), 0.55,0,Q100,1 },
            { TEXT("g = 0"), 0,0,Q100,1 },
            { TEXT("g > e"), 1,0.5,Q100,1 },
            { TEXT("g >> e"), 0.5,0,Q100,100 },
            { TEXT("steep, large e"), 3,1,1000.0/128.0,1 },
        };
        for (const FCase& C : Cases)
        {
            const Analysis::FSlope Slope = Analysis::SlopeFromGradient(C.GX,C.GY);
            const double Uncertainty = Analysis::SlopeUncertaintyDegrees(Slope,C.Q,C.D);
            const double E = FMath::Sqrt(2.0)*C.Q/(2.0*C.D);
            const double G = FMath::Sqrt(C.GX*C.GX+C.GY*C.GY);
            const double MinimumNorm = FMath::Max(0.0,G-E), MaximumNorm = G+E;
            // 境界: 大きさの最小・最大で角度のずれが最大になり、その大きい方が上限と一致する(上限は緩すぎない)。
            const double AtMinimum = Slope.SlopeDegrees - SlopeAngleOf(MinimumNorm), AtMaximum = SlopeAngleOf(MaximumNorm) - Slope.SlopeDegrees;
            TestEqual(*FString::Printf(TEXT("uncertainty equals the worse boundary: %s"),C.Name),Uncertainty,FMath::Max(AtMinimum,AtMaximum),1.e-9);
            // 内部と周上を走査しても上限を超えない。
            double Worst = 0;
            for (int32 Ring = 0; Ring <= 8; ++Ring)
                for (int32 Step = 0; Step < 72; ++Step)
                {
                    const double Radius = E*Ring/8.0, Angle = FMath::DegreesToRadians(Step*5.0);
                    const double Perturbed = Analysis::SlopeFromGradient(C.GX+Radius*FMath::Cos(Angle),C.GY+Radius*FMath::Sin(Angle)).SlopeDegrees;
                    Worst = FMath::Max(Worst,FMath::Abs(Perturbed-Slope.SlopeDegrees));
                }
            TestTrue(*FString::Printf(TEXT("uncertainty bounds the continuous error disc: %s"),C.Name),Worst <= Uncertainty + 1.e-9);
            if (G < E)
            {
                // 誤差領域が原点を含む: 真の勾配が0(真の傾斜0度)であり得る。
                TestEqual(*FString::Printf(TEXT("minimum norm is 0: %s"),C.Name),MinimumNorm,0.0,1.e-12);
                const double TrueZero = Analysis::SlopeFromGradient(C.GX-C.GX,C.GY-C.GY).SlopeDegrees;
                TestEqual(*FString::Printf(TEXT("true gradient can be 0: %s"),C.Name),TrueZero,0.0,1.e-12);
                TestTrue(*FString::Printf(TEXT("uncertainty covers a true slope of 0: %s"),C.Name),Uncertainty >= Slope.SlopeDegrees - 1.e-9);
                // 測定傾斜より小さい歩行可能角でも、真の傾斜0があり得るのでUNWALKABLEと断定しない。
                if (Slope.SlopeDegrees > 0.5)
                {
                    const Analysis::FWalkability W = Analysis::EvaluateWalkability(Slope.SlopeDegrees,Slope.SlopeDegrees*0.5,Uncertainty);
                    TestTrue(*FString::Printf(TEXT("g < e never classifies UNWALKABLE below the measured slope: %s"),C.Name),
                        W.Classification == Analysis::EWalkability::NearLimit);
                }
            }
        }
        // 同じ式を三角形の誤差にも使う。
        TestEqual(TEXT("gradient-error form matches the central-difference form"),
            Analysis::SlopeUncertaintyFromGradientError(0.5,FMath::Sqrt(2.0)*Q100/200.0),
            Analysis::SlopeUncertaintyDegrees(Analysis::SlopeFromGradient(0.5,0),Q100,100),1.e-12);
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

    // 29x29頂点、0〜2800cm四方。中心の頂点は(14,14) = (1400,1400)。
    const FLandscapeMCPResult Created = Create(LevelPath,TEXT("AI_HardeningLandscape"),FVector::ZeroVector,FVector(100,100,100),2,2,2,7,0,false);
    if (!TestTrue(*Created.Message,Created.bSuccess)) { return false; }
    const FString Path = Created.LandscapePath;
    ALandscape* Actor = FindObject<ALandscape>(nullptr,*Path);
    if (!TestNotNull(TEXT("created Landscape"),Actor)) { return false; }
    const int32 TotalTriangles = 28*28*2;

    // 各fixtureで共通に確認する: 独立計算との一致、GetSlopeとの一致。
    auto CheckNeighborhood = [&](const TCHAR* Name, double X, double Y, double Radius) -> FLandscapeMCPSlopeNeighborhoodResult
    {
        const FLandscapeMCPSlopeNeighborhoodResult N = SlopeNeighborhood(Path,X,Y,Radius,100);
        TestTrue(*FString::Printf(TEXT("%s: %s"),Name,*N.Message),N.bSuccess);
        const FExpectedLocal E = ExpectedLocal(Path,X,Y,Radius,28);
        TestEqual(*FString::Printf(TEXT("%s: localMax matches independent triangles"),Name),N.LocalMaxSlopeDegrees,E.Max,Tolerance);
        TestEqual(*FString::Printf(TEXT("%s: localMean matches independent triangles"),Name),N.LocalMeanSlopeDegrees,E.Mean,Tolerance);
        TestEqual(*FString::Printf(TEXT("%s: triangle count"),Name),N.TriangleCount,E.Count);
        const FLandscapeMCPSlopeResult S = Slope(Path,X,Y,100);
        TestEqual(*FString::Printf(TEXT("%s: centre slope equals GetSlope"),Name),N.CenterSlopeDegrees,S.SlopeDegrees,Tolerance);
        TestEqual(*FString::Printf(TEXT("%s: centre direction validity equals GetSlope"),Name),N.bCenterDirectionValid,S.bDirectionValid);
        TestEqual(*FString::Printf(TEXT("%s: centre height equals GetSlope"),Name),N.HeightCenterCm,S.HeightCenterCm,Tolerance);
        return N;
    };

    // 平地
    FLandscapeMCPSlopeNeighborhoodResult N = CheckNeighborhood(TEXT("flat"),1400,1400,300);
    TestEqual(TEXT("flat centre"),N.CenterSlopeDegrees,0.0,Tolerance);
    TestEqual(TEXT("flat localMax"),N.LocalMaxSlopeDegrees,0.0,Tolerance);
    TestEqual(TEXT("flat localMean"),N.LocalMeanSlopeDegrees,0.0,Tolerance);
    TestFalse(TEXT("flat is not clipped"),N.bClipped);
    FLandscapeMCPWalkabilityRegionResult WR = WalkabilityRegion(Path,0,0,2800,2800,44.77);
    TestTrue(*WR.Message,WR.bSuccess);
    TestEqual(TEXT("flat region triangles"),WR.TriangleCount,TotalTriangles);
    TestEqual(TEXT("flat region vertices"),WR.SampleCount,29*29);
    TestEqual(TEXT("flat region walkable"),WR.WalkableCount,TotalTriangles);
    TestEqual(TEXT("flat region ratio"),WR.WalkableRatio,1.0,Tolerance);
    TestEqual(TEXT("flat region classification"),WR.Classification,FString(TEXT("WALKABLE")));
    TestEqual(TEXT("flat region worst margin"),WR.WorstMarginDegrees,44.77,Tolerance);

    // 一方向斜面: +Xへ勾配0.5(26.565度)。中心傾斜と局所傾斜が一致する。
    if (!TestTrue(TEXT("ramp fixture"),SetHeightFixture(Actor,[](int32 X,int32) { return 64*X; }))) { return false; }
    const double Ramp = SlopeAngleOf(0.5);
    N = CheckNeighborhood(TEXT("ramp"),1400,1400,300);
    TestEqual(TEXT("ramp centre"),N.CenterSlopeDegrees,Ramp,Tolerance);
    TestEqual(TEXT("ramp localMax equals centre"),N.LocalMaxSlopeDegrees,Ramp,Tolerance);
    TestEqual(TEXT("ramp localMean equals centre"),N.LocalMeanSlopeDegrees,Ramp,Tolerance);
    TestEqual(TEXT("ramp max direction"),N.MaxSlopeDirectionDegrees,0.0,Tolerance);
    FLandscapeMCPHeightRegionResult Region = HeightRegion(Path,200,300,1000,700,200);
    TestTrue(*Region.Message,Region.bSuccess);
    TestEqual(TEXT("ramp region cell-average slope (v0.2 field)"),Region.MaxSlopeDegrees,Ramp,Tolerance);
    TestTrue(TEXT("ramp region triangle slope valid"),Region.bTriangleSlopeValid);
    TestEqual(TEXT("ramp region triangle slope"),Region.MaxTriangleSlopeDegrees,Ramp,Tolerance);
    TestEqual(TEXT("ramp region triangle count: 8x4 cells"),Region.TriangleCount,8*4*2);
    TestEqual(TEXT("ramp region v0.2 fields unchanged"),Region.MeanHeightCm,300.0,Tolerance);
    // 1点の領域でも、その点を含むセルの実三角形を評価する。
    Region = HeightRegion(Path,650,450,650,450,100);
    TestTrue(TEXT("point region keeps v0.2 behaviour"),Region.bSuccess && Region.SampleCount == 1 && !Region.bSlopeValid);
    TestTrue(TEXT("point region evaluates the containing cell"),Region.bTriangleSlopeValid && Region.TriangleCount == 2);
    TestEqual(TEXT("point region triangle slope"),Region.MaxTriangleSlopeDegrees,Ramp,Tolerance);
    // Region walkability: 閾値未満・超過・境界
    WR = WalkabilityRegion(Path,0,0,2800,2800,44.77);
    TestEqual(TEXT("ramp region below threshold"),WR.Classification,FString(TEXT("WALKABLE")));
    TestEqual(TEXT("ramp region max slope"),WR.MaxSlopeDegrees,Ramp,Tolerance);
    TestEqual(TEXT("ramp region worst margin"),WR.WorstMarginDegrees,44.77-Ramp,Tolerance);
    WR = WalkabilityRegion(Path,0,0,2800,2800,20);
    TestEqual(TEXT("ramp region above threshold"),WR.Classification,FString(TEXT("UNWALKABLE")));
    TestEqual(TEXT("ramp region unwalkable count"),WR.UnwalkableCount,TotalTriangles);
    TestEqual(TEXT("ramp region ratio 0"),WR.WalkableRatio,0.0,Tolerance);
    TestTrue(TEXT("ramp region negative margin"),WR.WorstMarginDegrees < 0);
    WR = WalkabilityRegion(Path,0,0,2800,2800,Ramp);
    TestEqual(TEXT("exact threshold: all near limit"),WR.NearLimitCount,TotalTriangles);
    TestEqual(TEXT("exact threshold: region is MIXED"),WR.Classification,FString(TEXT("MIXED")));
    TestEqual(TEXT("exact threshold: ratio excludes near limit"),WR.WalkableRatio,0.0,Tolerance);
    TestTrue(TEXT("triangle uncertainty reflects quantization"),WR.SlopeUncertaintyDegrees > 0.4 && WR.SlopeUncertaintyDegrees < 0.6);
    TestEqual(TEXT("threshold within uncertainty is MIXED"),WalkabilityRegion(Path,0,0,2800,2800,Ramp+0.3).Classification,FString(TEXT("MIXED")));
    TestEqual(TEXT("threshold clear of uncertainty is WALKABLE"),WalkabilityRegion(Path,0,0,2800,2800,Ramp+1).Classification,FString(TEXT("WALKABLE")));
    TestEqual(TEXT("threshold clearly below is UNWALKABLE"),WalkabilityRegion(Path,0,0,2800,2800,Ramp-1).Classification,FString(TEXT("UNWALKABLE")));

    // 半分だけ斜面: X<14は平地、X>=14は勾配0.5。
    if (!TestTrue(TEXT("half ramp fixture"),SetHeightFixture(Actor,[](int32 X,int32) { return 64*FMath::Max(0,X-14); }))) { return false; }
    WR = WalkabilityRegion(Path,0,0,2800,2800,20);
    TestTrue(*WR.Message,WR.bSuccess);
    TestEqual(TEXT("half ramp walkable count"),WR.WalkableCount,TotalTriangles/2);
    TestEqual(TEXT("half ramp unwalkable count"),WR.UnwalkableCount,TotalTriangles/2);
    TestEqual(TEXT("half ramp near limit count"),WR.NearLimitCount,0);
    TestEqual(TEXT("half ramp ratio"),WR.WalkableRatio,0.5,Tolerance);
    TestEqual(TEXT("half ramp classification"),WR.Classification,FString(TEXT("MIXED")));
    TestTrue(TEXT("half ramp worst location is on the ramp"),WR.WorstLocation.X > 1400);
    TestEqual(TEXT("flat half alone is WALKABLE"),WalkabilityRegion(Path,0,0,1400,2800,20).Classification,FString(TEXT("WALKABLE")));
    TestEqual(TEXT("ramp half alone is UNWALKABLE"),WalkabilityRegion(Path,1400,0,2800,2800,20).Classification,FString(TEXT("UNWALKABLE")));
    TestTrue(TEXT("region world bounds"),WR.WorldMin.Equals(FVector(0,0,0),Tolerance) && WR.WorldMax.Equals(FVector(2800,2800,700),Tolerance));

    // 丘の頂点: ピラミッド。頂点(14,14)が1000cm、1格子離れるごとに100cm下がる。
    if (!TestTrue(TEXT("peak fixture"),SetHeightFixture(Actor,[](int32 X,int32 Y) { return 128*FMath::Max(0,10-FMath::Max(FMath::Abs(X-14),FMath::Abs(Y-14))); }))) { return false; }
    N = CheckNeighborhood(TEXT("peak"),1400,1400,150);
    TestEqual(TEXT("peak: centre slope is 0"),N.CenterSlopeDegrees,0.0,Tolerance);
    TestFalse(TEXT("peak: centre has no direction"),N.bCenterDirectionValid);
    TestEqual(TEXT("peak: localMax finds the steep faces"),N.LocalMaxSlopeDegrees,Diagonal,Tolerance);
    TestTrue(TEXT("peak: localMean is steep but below localMax"),N.LocalMeanSlopeDegrees > 30.0 && N.LocalMeanSlopeDegrees < N.LocalMaxSlopeDegrees);
    TestTrue(TEXT("peak: max location is near the centre"),FVector2D::Distance(FVector2D(N.MaxSlopeLocation),FVector2D(1400,1400)) <= 150 + Tolerance);
    TestTrue(TEXT("peak: GetSlope alone reports walkable"),Walkability(Path,1400,1400,44.77,100).bWalkable);
    // 頂点を囲む2x2セル。max-normのピラミッドでは対角セルの片方の三角形が水平になるため、8枚中6枚が急斜面、2枚が水平。
    WR = WalkabilityRegion(Path,1300,1300,1500,1500,40);
    TestEqual(TEXT("peak: region triangle count"),WR.TriangleCount,8);
    TestEqual(TEXT("peak: region unwalkable count"),WR.UnwalkableCount,6);
    TestEqual(TEXT("peak: region walkable count"),WR.WalkableCount,2);
    TestEqual(TEXT("peak: region max slope"),WR.MaxSlopeDegrees,Diagonal,Tolerance);
    TestEqual(TEXT("peak: region around the apex is MIXED"),WR.Classification,FString(TEXT("MIXED")));
    // 半径が1セル未満でも、中心を含むセルの2枚は評価する。
    N = SlopeNeighborhood(Path,1450,1430,1,10);
    TestTrue(*N.Message,N.bSuccess);
    TestEqual(TEXT("tiny radius evaluates the containing cell"),N.TriangleCount,2);
    TestEqual(TEXT("tiny radius local max"),N.LocalMaxSlopeDegrees,45.0,Tolerance);

    // V字谷底: X=14の線が谷。両側は45度。
    if (!TestTrue(TEXT("valley fixture"),SetHeightFixture(Actor,[](int32 X,int32) { return 128*FMath::Abs(X-14); }))) { return false; }
    N = CheckNeighborhood(TEXT("valley"),1400,1400,200);
    TestEqual(TEXT("valley: centre slope is 0"),N.CenterSlopeDegrees,0.0,Tolerance);
    TestEqual(TEXT("valley: localMax"),N.LocalMaxSlopeDegrees,45.0,Tolerance);
    TestEqual(TEXT("valley: localMean"),N.LocalMeanSlopeDegrees,45.0,Tolerance);

    // 尾根: X=14の線が稜線。
    if (!TestTrue(TEXT("ridge fixture"),SetHeightFixture(Actor,[](int32 X,int32) { return 128*(14-FMath::Abs(X-14)); }))) { return false; }
    N = CheckNeighborhood(TEXT("ridge"),1400,1400,200);
    TestEqual(TEXT("ridge: centre slope is 0"),N.CenterSlopeDegrees,0.0,Tolerance);
    TestEqual(TEXT("ridge: localMax"),N.LocalMaxSlopeDegrees,45.0,Tolerance);
    TestEqual(TEXT("ridge: region classification"),WalkabilityRegion(Path,1200,1200,1600,1600,44).Classification,FString(TEXT("UNWALKABLE")));

    // 鞍部: z = (dx^2 - dy^2)。中心はX方向に谷、Y方向に尾根。
    if (!TestTrue(TEXT("saddle fixture"),SetHeightFixture(Actor,[](int32 X,int32 Y) { return 32*((X-14)*(X-14)-(Y-14)*(Y-14)); }))) { return false; }
    N = CheckNeighborhood(TEXT("saddle"),1400,1400,250);
    TestEqual(TEXT("saddle: centre slope is 0"),N.CenterSlopeDegrees,0.0,Tolerance);
    TestFalse(TEXT("saddle: centre has no direction"),N.bCenterDirectionValid);
    TestTrue(TEXT("saddle: localMax finds the surrounding slope"),N.LocalMaxSlopeDegrees > 30.0);
    TestTrue(TEXT("saddle: localMean is between 0 and localMax"),N.LocalMeanSlopeDegrees > 0 && N.LocalMeanSlopeDegrees < N.LocalMaxSlopeDegrees);

    // 4隅[0,100,100,0]型セル: セル(10,10)の頂点(11,10)と(10,11)だけ100cm。
    if (!TestTrue(TEXT("twisted cell fixture"),SetHeightFixture(Actor,[](int32 X,int32 Y) { return ((X == 11 && Y == 10) || (X == 10 && Y == 11)) ? 128 : 0; }))) { return false; }
    Region = HeightRegion(Path,1000,1000,1100,1100,100);
    TestTrue(*Region.Message,Region.bSuccess);
    TestEqual(TEXT("twisted cell: 4 samples"),Region.SampleCount,4);
    TestEqual(TEXT("twisted cell: cell-average slope is 0 (v0.2 field unchanged)"),Region.MaxSlopeDegrees,0.0,Tolerance);
    TestEqual(TEXT("twisted cell: triangle slope finds it"),Region.MaxTriangleSlopeDegrees,Diagonal,Tolerance);
    TestEqual(TEXT("twisted cell: 2 triangles"),Region.TriangleCount,2);
    TestTrue(TEXT("twisted cell: triangle location inside the cell"),Region.MaxTriangleSlopeLocation.X > 1000 && Region.MaxTriangleSlopeLocation.X < 1100
        && Region.MaxTriangleSlopeLocation.Y > 1000 && Region.MaxTriangleSlopeLocation.Y < 1100);
    N = CheckNeighborhood(TEXT("twisted cell"),1050,1050,10);
    TestEqual(TEXT("twisted cell: neighbourhood localMax"),N.LocalMaxSlopeDegrees,Diagonal,Tolerance);
    TestEqual(TEXT("twisted cell: region walkability"),WalkabilityRegion(Path,1000,1000,1100,1100,44.77).Classification,FString(TEXT("UNWALKABLE")));
    // Engineの実Collisionが対角線00-11で分割されていること。セル中心は対角線上にあり、高さは0cm。
    // bilinearでは50cm、逆の対角線なら100cmになる。
    {
        FHitResult Hit;
        const FCollisionQueryParams Query(FName(TEXT("LandscapeMCPDiagonal")),true);
        if (TestTrue(TEXT("collision trace hits the twisted cell"),World->LineTraceSingleByChannel(Hit,FVector(1050,1050,1000),FVector(1050,1050,-1000),ECC_WorldStatic,Query)))
        { TestEqual(TEXT("collision splits the cell along diagonal 00-11"),double(Hit.ImpactPoint.Z),0.0,1.0); }
        // 頂点10寄りの点はLower三角形(00,10,11)上: z = 100*(u-v) = 50cm。法線の傾きは三角形の傾斜と一致する。
        if (TestTrue(TEXT("collision trace hits the lower triangle"),World->LineTraceSingleByChannel(Hit,FVector(1075,1025,1000),FVector(1075,1025,-1000),ECC_WorldStatic,Query)))
        {
            TestEqual(TEXT("collision lower triangle height"),double(Hit.ImpactPoint.Z),50.0,1.0);
            TestEqual(TEXT("collision normal matches the triangle slope"),FMath::RadiansToDegrees(FMath::Acos(double(Hit.ImpactNormal.Z))),Diagonal,1.0);
        }
        TestEqual(TEXT("bilinear source height differs from collision at the cell centre"),Height(Path,1050,1050).HeightCm,50.0,Tolerance);
    }

    // 入力Validation
    TestFalse(TEXT("neighbourhood zero radius"),SlopeNeighborhood(Path,1400,1400,0,100).bSuccess);
    TestFalse(TEXT("neighbourhood negative radius"),SlopeNeighborhood(Path,1400,1400,-1,100).bSuccess);
    TestFalse(TEXT("neighbourhood oversized radius"),SlopeNeighborhood(Path,1400,1400,5001,100).bSuccess);
    TestFalse(TEXT("neighbourhood NaN radius"),SlopeNeighborhood(Path,1400,1400,NaN,100).bSuccess);
    TestFalse(TEXT("neighbourhood infinite radius"),SlopeNeighborhood(Path,1400,1400,Infinity,100).bSuccess);
    TestFalse(TEXT("neighbourhood invalid distance"),SlopeNeighborhood(Path,1400,1400,300,0).bSuccess);
    TestFalse(TEXT("neighbourhood NaN X"),SlopeNeighborhood(Path,NaN,1400,300,100).bSuccess);
    TestFalse(TEXT("neighbourhood centre outside"),SlopeNeighborhood(Path,2900,1400,300,100).bSuccess);
    TestFalse(TEXT("neighbourhood centre stencil crosses edge"),SlopeNeighborhood(Path,50,1400,300,100).bSuccess);
    TestFalse(TEXT("neighbourhood name instead of path"),SlopeNeighborhood(TEXT("AI_HardeningLandscape"),1400,1400,300,100).bSuccess);
    N = SlopeNeighborhood(Path,200,1400,500,100);
    TestTrue(TEXT("neighbourhood radius crossing the edge is clipped, not rejected"),N.bSuccess && N.bClipped);
    TestFalse(TEXT("region reversed X"),WalkabilityRegion(Path,1000,0,200,700,45).bSuccess);
    TestFalse(TEXT("region reversed Y"),WalkabilityRegion(Path,0,700,1000,300,45).bSuccess);
    TestFalse(TEXT("region NaN bound"),WalkabilityRegion(Path,NaN,0,1000,700,45).bSuccess);
    TestFalse(TEXT("region infinite bound"),WalkabilityRegion(Path,0,0,Infinity,700,45).bSuccess);
    TestFalse(TEXT("region out of range"),WalkabilityRegion(Path,0,0,2900,700,45).bSuccess);
    TestFalse(TEXT("region negative angle"),WalkabilityRegion(Path,0,0,1000,700,-1).bSuccess);
    TestFalse(TEXT("region angle above 90"),WalkabilityRegion(Path,0,0,1000,700,90.1).bSuccess);
    TestFalse(TEXT("region NaN angle"),WalkabilityRegion(Path,0,0,1000,700,NaN).bSuccess);
    TestFalse(TEXT("region missing path"),WalkabilityRegion(LevelPath+TEXT(".Missing"),0,0,1000,700,45).bSuccess);
    WR = WalkabilityRegion(Path,650,450,650,450,45);
    TestTrue(TEXT("point region evaluates the containing cell"),WR.bSuccess && WR.TriangleCount == 2);

    // 読み取り専用: Package、Undo履歴、他Actorを変更しない
    World->GetOutermost()->SetDirtyFlag(false);
    const int32 UndoBefore = UToolsetLibrary::GetActiveUndoCount();
    TestTrue(TEXT("read neighbourhood"),SlopeNeighborhood(Path,1400,1400,500,100).bSuccess);
    TestTrue(TEXT("read walkability region"),WalkabilityRegion(Path,0,0,2800,2800,44.77).bSuccess);
    TestTrue(TEXT("read height region with triangles"),HeightRegion(Path,0,0,2800,2800,100).bSuccess);
    TestFalse(TEXT("failed neighbourhood read"),SlopeNeighborhood(Path,50,1400,300,100).bSuccess);
    TestFalse(TEXT("failed region read"),WalkabilityRegion(Path,0,0,2900,700,45).bSuccess);
    TestFalse(TEXT("read-only operations do not dirty package"),World->GetOutermost()->IsDirty());
    TestEqual(TEXT("read-only operations do not create Undo entries"),UToolsetLibrary::GetActiveUndoCount(),UndoBefore);
    TestTrue(TEXT("sentinel Actor unchanged"),Sentinel->GetActorTransform().Equals(SentinelBefore));

    // v0.1のSafety境界を共有する
    ULandscapeEditLayerBase* Layer = Actor->GetEditLayer(0);
    Layer->SetLocked(true,false);
    TestFalse(TEXT("locked layer refuses neighbourhood"),SlopeNeighborhood(Path,1400,1400,300,100).bSuccess);
    TestFalse(TEXT("locked layer refuses walkability region"),WalkabilityRegion(Path,0,0,1000,700,45).bSuccess);
    Layer->SetLocked(false,false);
    Actor->SetActorRotation(FRotator(0,10,0));
    TestFalse(TEXT("rotated Landscape refuses neighbourhood"),SlopeNeighborhood(Path,1400,1400,300,100).bSuccess);
    TestFalse(TEXT("rotated Landscape refuses walkability region"),WalkabilityRegion(Path,0,0,1000,700,45).bSuccess);
    Actor->SetActorRotation(FRotator::ZeroRotator);
    TestTrue(TEXT("neighbourhood available again"),SlopeNeighborhood(Path,1400,1400,300,100).bSuccess);

    // Sample上限: 249x249 = 62001頂点のLandscape。Scale(20,20,100)で49.6m四方。
    {
        const FLandscapeMCPResult Large = Create(LevelPath,TEXT("AI_LargeLandscape"),FVector(20000,0,0),FVector(20,20,100),4,4,2,31,0,false);
        if (!TestTrue(*Large.Message,Large.bSuccess)) { return false; }
        TestEqual(TEXT("large Landscape vertices"),Large.SampleCount,249*249);
        TestFalse(TEXT("oversized walkability region rejected"),WalkabilityRegion(Large.LandscapePath,20000,0,24960,4960,45).bSuccess);
        TestFalse(TEXT("oversized neighbourhood rejected"),SlopeNeighborhood(Large.LandscapePath,22480,2480,5000,100).bSuccess);
        // 上限ちょうど: 128x128頂点 = 127x127セル。
        WR = WalkabilityRegion(Large.LandscapePath,20000,0,20000+127*20,127*20,45);
        TestTrue(*WR.Message,WR.bSuccess);
        TestEqual(TEXT("region at the vertex limit"),WR.SampleCount,16384);
        TestEqual(TEXT("region at the vertex limit triangles"),WR.TriangleCount,127*127*2);
        TestFalse(TEXT("region one cell over the limit rejected"),WalkabilityRegion(Large.LandscapePath,20000,0,20000+128*20,127*20,45).bSuccess);
        N = SlopeNeighborhood(Large.LandscapePath,22480,2480,1000,100);
        TestTrue(TEXT("neighbourhood within the limit"),N.bSuccess && N.SampleCount <= 16384);
        // GetHeightRegionはv0.2で成功していた入力を拒否しない。実三角形は全域を評価する。
        Region = HeightRegion(Large.LandscapePath,20000,0,24960,4960,200);
        TestTrue(*Region.Message,Region.bSuccess);
        TestEqual(TEXT("large height region samples (v0.2 behaviour)"),Region.SampleCount,25*25);
        TestEqual(TEXT("large height region triangles"),Region.TriangleCount,248*248*2);
        TestFalse(TEXT("v0.2 sample limit still applies"),HeightRegion(Large.LandscapePath,20000,0,24960,4960,20).bSuccess);
    }
    TestEqual(TEXT("no Level / Asset save events"),SaveEvents,0);

    // schema: 既存8 Toolを保持し、新2 Toolを公開する
    const FString Schema = UToolsetRegistry::GetToolsetJsonSchema(ULandscapeMCPToolset::StaticClass());
    for (const TCHAR* Tool : { TEXT("CreateLandscape"),TEXT("GetHeight"),TEXT("SculptRegion"),TEXT("SmoothRegion"),TEXT("FlattenRegion"),
        TEXT("GetSlope"),TEXT("GetHeightRegion"),TEXT("EvaluateWalkability"),TEXT("AnalyzeSlopeNeighborhood"),TEXT("EvaluateWalkabilityRegion") })
    { TestTrue(*FString::Printf(TEXT("official schema includes %s"),Tool),Schema.Contains(Tool)); }
    TestTrue(TEXT("schema exposes the new triangle field"),Schema.Contains(TEXT("maxTriangleSlopeDegrees")));
    TestEqual(TEXT("toolset version"),GetDefault<ULandscapeMCPToolset>()->GetToolsetVersion(),FString(TEXT("0.3")));
    // facade経由でも同じ値
    TestEqual(TEXT("facade neighbourhood"),ULandscapeMCPToolset::AnalyzeSlopeNeighborhood(Path,1050,1050,10,10).LocalMaxSlopeDegrees,Diagonal,Tolerance);
    TestEqual(TEXT("facade walkability region"),ULandscapeMCPToolset::EvaluateWalkabilityRegion(Path,1000,1000,1100,1100,44.77).Classification,FString(TEXT("UNWALKABLE")));
    AddInfo(Schema);
    return true;
}
#endif
