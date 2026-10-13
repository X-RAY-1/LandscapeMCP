#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "LandscapeMCPTerrainPlanning.h"
#include "LandscapeMCPTerrainAnalysis.h"
#include "LandscapeMCPOperations.h"
#include "LandscapeMCPToolset.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Landscape.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/AutomationEditorCommon.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "ToolsetRegistry/ToolsetLibrary.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#include <limits>

namespace LandscapeMCP::Phase1Tests
{
    using namespace Authoring;
    FHeightfieldSnapshot Snapshot(int32 Count=64,FVector Scale=FVector(100))
    {
        FHeightfieldSnapshot S;S.Scale=Scale;S.Extent=S.Rect=FIntRect(0,0,Count-1,Count-1);S.Heights.Init(32768,Count*Count);return S;
    }
    FPreviewRequest PathRequest()
    {
        FPreviewRequest R;R.Points={FVector2D(1500,3000),FVector2D(4500,3000)};
        R.Width=400;R.Falloff=500;R.TargetHeight=100;R.MaxDelta=300;return R;
    }
    FPreviewRequest RectangleRequest()
    {
        FPreviewRequest R;R.Operation=EAuthoringOperation::FlattenRectangle;R.Center=FVector2D(3000,3000);
        R.SizeX=1000;R.SizeY=800;R.Falloff=300;R.TargetHeight=100;R.MaxDelta=300;return R;
    }
    void Fill(FHeightfieldSnapshot& S,TFunctionRef<double(int32,int32)> Function)
    {
        for(int32 Y=0;Y<S.CountY();++Y)for(int32 X=0;X<S.CountX();++X)
            EncodeHeight(Function(X,Y),S.Origin.Z,S.Scale.Z,S.Heights[Y*S.CountX()+X]);
    }
}

using namespace LandscapeMCP;
using namespace LandscapeMCP::Authoring;
using namespace LandscapeMCP::Phase1Tests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04Sampling,"LandscapeMCP.V04.PathSampling",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04Sampling::RunTest(const FString& Parameters)
{
    FPath P;FString Code;TArray<FStation> S;
    const TArray<FVector2D> Points={FVector2D(0,0),FVector2D(155,0),FVector2D(455,100)};
    TestTrue(TEXT("valid bend"),ValidatePath(Points,20,P,Code));
    TestEqual(TEXT("XY arc length"),P.Length,155+FMath::Sqrt(100000.0),1.e-8);
    TestTrue(TEXT("sample"),SamplePath(P,100,S,Code));
    TestEqual(TEXT("knots preserved"),S[2].Position,Points[1]);
    TestEqual(TEXT("last point"),S.Last().Position,Points.Last());
    for(int32 I=1;I<S.Num();++I){TestTrue(TEXT("monotonic"),S[I].Distance>S[I-1].Distance);TestTrue(TEXT("never coarser"),S[I].Distance-S[I-1].Distance<=100+1.e-9);}
    TestEqual(TEXT("projection"),SegmentProjection(FVector2D(50,20),FVector2D(0,0),FVector2D(100,0)),0.5);
    TestEqual(TEXT("clamped round endpoint"),SegmentProjection(FVector2D(-50,0),FVector2D(0,0),FVector2D(100,0)),0.0);
    TestFalse(TEXT("no automatic coarse sampling"),SamplePath(P,0.1,S,Code));TestEqual(TEXT("cap code"),Code,FString(TEXT("SAMPLE_LIMIT_EXCEEDED")));
    TestFalse(TEXT("degenerate rejected"),ValidatePath(TArray<FVector2D>{FVector2D(0,0),FVector2D(0.5,0)},10,P,Code));
    TestEqual(TEXT("degenerate code"),Code,FString(TEXT("DEGENERATE_SEGMENT")));
    TestFalse(TEXT("U turn rejected"),ValidatePath(TArray<FVector2D>{FVector2D(0,0),FVector2D(200,0),FVector2D(100,0)},10,P,Code));
    TestFalse(TEXT("intersection rejected"),ValidatePath(TArray<FVector2D>{FVector2D(0,0),FVector2D(200,200),FVector2D(0,200),FVector2D(200,0)},10,P,Code));
    TestFalse(TEXT("near non-adjacent overlap rejected"),ValidatePath(TArray<FVector2D>{FVector2D(0,0),FVector2D(300,0),FVector2D(600,100),FVector2D(900,100)},200,P,Code));
    TestFalse(TEXT("path length cap"),ValidatePath(TArray<FVector2D>{FVector2D(0,0),FVector2D(10001,0)},10,P,Code));
    TArray<FVector2D> Many;for(int32 I=0;I<65;++I)Many.Add(FVector2D(I*100,0));
    TestFalse(TEXT("point cap"),ValidatePath(Many,10,P,Code));
    TestTrue(TEXT("1024 samples exact"),ValidatePath(TArray<FVector2D>{FVector2D(0,0),FVector2D(1023,0)},0.1,P,Code)&&SamplePath(P,1,S,Code));
    TestEqual(TEXT("1024 count"),S.Num(),1024);
    TestTrue(TEXT("1025 path valid"),ValidatePath(TArray<FVector2D>{FVector2D(0,0),FVector2D(1024,0)},0.1,P,Code));
    TestFalse(TEXT("1025 samples reject"),SamplePath(P,1,S,Code));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04Geometry,"LandscapeMCP.V04.GeometryMasks",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04Geometry::RunTest(const FString& Parameters)
{
    // 独立した既知座標。各Scaleに対しpositive areaと接触を区別する。
    for(double Scale:{1.0,100.0,1000.0})
    {
        const FVector2D A(0,0),B(Scale,0),C(Scale,Scale),G(Scale,Scale);
        TestTrue(TEXT("edge touch excluded"),TriangleRectangle(A,B,C,FVector2D(Scale,0),FVector2D(2*Scale,Scale),G)==EIntersection::None);
        TestTrue(TEXT("corner touch excluded"),TriangleRectangle(A,B,C,FVector2D(Scale,Scale),FVector2D(2*Scale,2*Scale),G)==EIntersection::None);
        TestTrue(TEXT("narrow positive overlap"),TriangleRectangle(A,B,C,FVector2D(Scale*(1-1.e-7),0),FVector2D(2*Scale,Scale),G)==EIntersection::Positive);
        TestTrue(TEXT("uncertain tiny overlap fails closed"),TriangleRectangle(A,B,C,FVector2D(Scale*(1-1.e-13),0),FVector2D(2*Scale,Scale),G)==EIntersection::Ambiguous);
        FPath P;FString Code;ValidatePath(TArray<FVector2D>{FVector2D(-10*Scale,-Scale),FVector2D(10*Scale,-Scale)},0.1,P,Code);
        // Scale=1000ではpath length capが効くのでmask用には幾何を直接与える。
        P.Points={FVector2D(-Scale,-Scale),FVector2D(2*Scale,-Scale)};P.Distances={0,3*Scale};P.Length=3*Scale;
        TestTrue(TEXT("capsule tangent excluded"),TrianglePath(A,B,C,P,Scale,G)==EIntersection::None);
        TestTrue(TEXT("capsule narrow overlap"),TrianglePath(A,B,C,P,Scale*(1+1.e-7),G)==EIntersection::Positive);
        TestTrue(TEXT("capsule ambiguous overlap rejected"),TrianglePath(A,B,C,P,Scale*(1+1.e-12),G)==EIntersection::Ambiguous);
    }
    FPath P;FString Code;ValidatePath(TArray<FVector2D>{FVector2D(0,0),FVector2D(1000,0),FVector2D(2000,500)},200,P,Code);
    TArray<FStation> S;SamplePath(P,100,S,Code);TArray<double> H;for(const auto& V:S)H.Add(V.Distance/10);
    const double L=JoinedHeight(P,S,H,FVector2D(1000,100-1.e-5),200),R=JoinedHeight(P,S,H,FVector2D(1000,100+1.e-5),200);
    TestTrue(TEXT("join continuity"),FMath::Abs(L-R)<1.e-3);
    TestTrue(TEXT("round cap distance"),FMath::Abs(PathDistance(P,FVector2D(-100,0))-100)<1.e-8);
    const auto Snap=Snapshot();auto Request=RectangleRequest();Request.Center=FVector2D(100,100);Request.SizeX=400;Request.SizeY=400;
    const auto Clipped=PlanPreview(Request,Snap);TestTrue(TEXT("flatten clip accepted"),Clipped.bSuccess&&Clipped.bClipped&&Clipped.ClippedSampleCount>0);
    Request.Operation=EAuthoringOperation::CreateWalkablePath;Request.Points={FVector2D(100,100),FVector2D(1000,100)};Request.Width=400;Request.MaxSlope=30;
    TestEqual(TEXT("core clipping fails closed"),PlanPreview(Request,Snap).ErrorCode,FString(TEXT("INCOMPLETE_WALKABLE_CORRIDOR")));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04Falloff,"LandscapeMCP.V04.FalloffAndQuantization",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04Falloff::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("core"),FalloffWeight(0,100),1.0);TestEqual(TEXT("edge"),FalloffWeight(100,100),0.0);
    TestEqual(TEXT("smooth midpoint"),FalloffWeight(50,100),0.5);TestEqual(TEXT("hard falloff"),FalloffWeight(1,0),0.0);
    uint16 E;TestTrue(TEXT("encode minimum"),EncodeHeight(-25600,0,100,E)&&E==0);
    TestTrue(TEXT("encode maximum"),EncodeHeight((65535.0-32768)*100/128,0,100,E)&&E==65535);
    TestFalse(TEXT("overflow not clamped"),EncodeHeight(25600,0,100,E));
    auto R=RectangleRequest();auto S=Snapshot();R.TargetHeight=0.5;R.MaxDelta=0.6;
    TestEqual(TEXT("quantized maxDelta enforced"),PlanPreview(R,S).ErrorCode,FString(TEXT("MAX_DELTA_EXCEEDED")));
    R.MaxDelta=1;TestTrue(TEXT("rounding within budget"),PlanPreview(R,S).bSuccess);
    R.Yaw=360;TestEqual(TEXT("yaw no normalization"),PlanPreview(R,S).ErrorCode,FString(TEXT("UNSUPPORTED_RECTANGLE_YAW")));
    R.Yaw=0;R.SizeX=0;TestEqual(TEXT("zero dimensions"),PlanPreview(R,S).ErrorCode,FString(TEXT("INVALID_DIMENSIONS")));
    R.SizeX=1000;R.TargetHeight=std::numeric_limits<double>::quiet_NaN();TestFalse(TEXT("NaN rejected"),PlanPreview(R,S).bSuccess);
    R=RectangleRequest();R.Falloff=2500;R.SizeX=5000;R.SizeY=5000;TestEqual(TEXT("request sample cap"),PlanPreview(R,Snapshot(128,FVector(25,25,100))).ErrorCode,FString(TEXT("SAMPLE_LIMIT_EXCEEDED")));
    R=RectangleRequest();S.CachedSourceBytes=MaxCachedSourceBytes+1;TestEqual(TEXT("mip budget"),PlanPreview(R,S).ErrorCode,FString(TEXT("INVALID_SNAPSHOT")));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04Modes,"LandscapeMCP.V04.ProfileModes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04Modes::RunTest(const FString& Parameters)
{
    auto S=Snapshot();Fill(S,[](int32 X,int32 Y){return double(X)*2;});auto R=PathRequest();
    auto O=PlanPreview(R,S);TestTrue(TEXT("fixed preview"),O.bSuccess);
    for(const auto& V:O.Path.ProfileSamples)TestEqual(TEXT("fixed profile"),V.TargetHeightCm,100.0);
    R.HeightMode=TEXT("follow_start");R.TargetHeight=999;O=PlanPreview(R,S);TestTrue(TEXT("non-fixed ignores finite target"),O.bSuccess);
    for(const auto& V:O.Path.ProfileSamples)TestEqual(TEXT("start height"),V.TargetHeightCm,29.6875,1.e-8);
    // knot間の地形に凹凸を追加しても、profileはknot高度の線形補間。
    Fill(S,[](int32 X,int32 Y){return X==30?400:double(X)*2;});R.Points.Insert(FVector2D(2500,3000),1);
    R.HeightMode=TEXT("follow_path");R.MaxDelta=500;O=PlanPreview(R,S);TestTrue(TEXT("follow path preview"),O.bSuccess);
    for(const auto& V:O.Path.ProfileSamples)
    {
        const double Expected=V.DistanceCm<=1000?FMath::Lerp(29.6875,50.0,V.DistanceCm/1000):FMath::Lerp(50.0,89.84375,(V.DistanceCm-1000)/2000);
        TestEqual(TEXT("snapshot knot interpolation"),V.TargetHeightCm,Expected,1.e-8);
    }
    TestTrue(TEXT("snapshot immutable"),S.Decode(S.Heights[30+30*S.CountX()])==400);
    R.HeightMode=TEXT("explicit");TestEqual(TEXT("unsupported mode"),PlanPreview(R,S).ErrorCode,FString(TEXT("INVALID_HEIGHT_MODE")));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04Planner,"LandscapeMCP.V04.WalkableProfilePlanner",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04Planner::RunTest(const FString& Parameters)
{
    const TArray<FStation> S={ {FVector2D(0,0),0},{FVector2D(1000,0),1000},{FVector2D(2000,0),2000} };
    TArray<double> Target,Again;const TArray<double> Current={0,1000,0};
    TestTrue(TEXT("forward backward feasible"),PlanWalkableProfile(S,Current,800,30,-10000,10000,Target));
    TestEqual(TEXT("start anchored"),Target[0],0.0);TestEqual(TEXT("end anchored"),Target.Last(),0.0);
    TestTrue(TEXT("profile slope improves"),Target[1]<1000&&Target[1]>0);
    PlanWalkableProfile(S,Current,800,30,-10000,10000,Again);TestTrue(TEXT("deterministic"),Target==Again);
    TestFalse(TEXT("maxDelta impossible"),PlanWalkableProfile(S,Current,100,30,-10000,10000,Target));
    TestFalse(TEXT("endpoint grade impossible"),PlanWalkableProfile(S,TArray<double>{0,0,2000},1000,10,-10000,10000,Target));
    auto R=PathRequest();R.Operation=EAuthoringOperation::CreateWalkablePath;R.MaxSlope=30;const auto Snap=Snapshot();
    const auto O=PlanPreview(R,Snap);TestTrue(TEXT("flat corridor safe"),O.bSuccess&&O.bWalkabilityGuaranteed);
    TestEqual(TEXT("ratio1"),O.PlannedAfter.WalkableRatio,1.0);TestEqual(TEXT("near limit0"),O.PlannedAfter.NearLimitCount,0);
    TestEqual(TEXT("unwalkable0"),O.PlannedAfter.UnwalkableCount,0);
    auto Peak=Snapshot();Fill(Peak,[](int32 X,int32 Y){return X==30&&Y==30?200.0:0.0;});
    const auto Improved=PlanPreview(R,Peak);TestTrue(*Improved.ErrorCode,Improved.bSuccess);
    TestTrue(TEXT("nontrivial walkable plan"),Improved.PlannedModifiedCount>0);
    TestTrue(TEXT("native slope improvement"),Improved.PlannedAfter.MaxTriangleSlopeDegrees<Improved.Before.MaxTriangleSlopeDegrees);
    TestEqual(TEXT("improved core ratio1"),Improved.PlannedAfter.WalkableRatio,1.0);
    auto Steep=Snapshot();Fill(Steep,[](int32 X,int32 Y){return X*100.0;});R.MaxDelta=100;
    TestEqual(TEXT("unsafe endpoints rejected"),PlanPreview(R,Steep).ErrorCode,FString(TEXT("CANNOT_SATISFY_WALKABILITY")));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04Triangles,"LandscapeMCP.V04.PlannedTriangleEvaluation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04Triangles::RunTest(const FString& Parameters)
{
    auto S=Snapshot();Fill(S,[](int32 X,int32 Y){return X==30&&Y==30?100.0:0.0;});auto R=RectangleRequest();R.TargetHeight=0;
    const auto O=PlanPreview(R,S);TestTrue(TEXT("peak flatten preview"),O.bSuccess);
    TestTrue(TEXT("before native slope positive"),O.Before.MaxTriangleSlopeDegrees>40);
    TestEqual(TEXT("planned native slope flat"),O.PlannedAfter.MaxTriangleSlopeDegrees,0.0);
    TestEqual(TEXT("rect 10x8 core cells"),O.PlannedAfter.TriangleCount,160);
    const auto T=Analysis::CellTriangleSlopes(0,100,100,0,100,100);
    TestEqual(TEXT("00-11 twisted cell"),T.Lower.SlopeDegrees,FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(2.0))),1.e-8);
    const double Error=Analysis::TriangleGradientError(100.0/128,100,100);
    const double U=Analysis::SlopeUncertaintyFromGradientError(0,Error);
    TestTrue(TEXT("uncertainty boundary not success"),Analysis::EvaluateWalkability(0,U,U).Classification==Analysis::EWalkability::NearLimit);
    R=PathRequest();R.Operation=EAuthoringOperation::CreateWalkablePath;R.MaxSlope=0.1;
    TestFalse(TEXT("uncertainty reserve too large reject"),PlanPreview(R,Snapshot()).bSuccess);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04Contract,"LandscapeMCP.V04.OperationResultContract",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04Contract::RunTest(const FString& Parameters)
{
    auto R=PathRequest();const auto O=PlanPreview(R,Snapshot());TestTrue(TEXT("valid plan"),O.bSuccess&&O.bPlanValid&&O.bDryRun);
    TestFalse(TEXT("no actual mutation"),O.bActualValid||O.ActualAfter.bValid);TestEqual(TEXT("modified zero"),O.ModifiedCount,0);
    TestEqual(TEXT("clipping count equation"),O.RequestedSampleCount-O.AffectedSampleCount,O.ClippedSampleCount);
    TestTrue(TEXT("requested>=affected>0"),O.RequestedSampleCount>=O.AffectedSampleCount&&O.AffectedSampleCount>0);
    TestTrue(TEXT("read includes halo"),O.ReadSampleCount>O.AffectedSampleCount);
    TestTrue(TEXT("planned modifications"),O.PlannedModifiedCount>0&&O.PlannedModifiedCount<=O.AffectedSampleCount);
    TestEqual(TEXT("path count"),O.Path.ProfileSampleCount,O.Path.ProfileSamples.Num());
    TestTrue(TEXT("all preview bounds valid"),O.RequestedBounds.bValid&&O.AffectedBounds.bValid&&O.ReadBounds.bValid&&O.VerificationBounds.bValid);
    TestFalse(TEXT("flatten no implicit walkability threshold"),O.PlannedAfter.bWalkabilityValid||O.bWalkabilityGuaranteed);
    TestTrue(TEXT("delta signed"),O.Delta.MaxRaiseCm>0&&O.Delta.MaxLowerCm==0&&O.Delta.MinDeltaCm>=0);
    auto Hard=RectangleRequest();Hard.Falloff=0;const auto Raised=PlanPreview(Hard,Snapshot());
    TestEqual(TEXT("all changed positive minimum"),Raised.Delta.MinDeltaCm,100.0);TestEqual(TEXT("no negative maxLower"),Raised.Delta.MaxLowerCm,0.0);
    auto Failing=PathRequest();Failing.MaxDelta=50;const auto Rejected=PlanPreview(Failing,Snapshot());
    TestFalse(TEXT("incomplete profile invalid"),Rejected.Path.bValid);
    TestEqual(TEXT("rejected profile count consistent"),Rejected.Path.ProfileSampleCount,Rejected.Path.ProfileSamples.Num());
    R.bDryRun=false;TestEqual(TEXT("apply fails before target resolution"),Preview(R).ErrorCode,FString(TEXT("APPLY_DISABLED_PHASE1")));
    R=RectangleRequest();const auto Rect=PlanPreview(R,Snapshot());TestFalse(TEXT("rectangle no path payload"),Rect.Path.bValid);
    TestTrue(TEXT("rectangle profile empty"),Rect.Path.ProfileSamples.IsEmpty());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04Compatibility,"LandscapeMCP.V04.CoordinateHeightTriangleCompatibility",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04Compatibility::RunTest(const FString& Parameters)
{
    auto S=Snapshot(4,FVector(200,50,25));S.Origin=FVector(-5000,3000,100);
    S.Heights[0]=32768;S.Heights[1]=33280;S.Heights[4]=33280;S.Heights[5]=32768;
    double H;TestTrue(TEXT("bilinear sample"),S.Sample(FVector2D(-4900,3025),S.Heights,H));TestEqual(TEXT("bilinear center not collision"),H,150.0);
    TestTrue(TEXT("inclusive last vertex"),S.Sample(FVector2D(-4400,3150),S.Heights,H));
    TestFalse(TEXT("outside extent"),S.Sample(FVector2D(-4400.1,3151),S.Heights,H));
    for(uint16 Encoded:{uint16(0),uint16(1),uint16(32768),uint16(65535)})
    {uint16 RoundTrip;TestTrue(TEXT("encode decode unchanged"),EncodeHeight(S.Decode(Encoded),S.Origin.Z,S.Scale.Z,RoundTrip)&&RoundTrip==Encoded);}
    const auto Tri=Analysis::CellTriangleSlopes(0,100,100,0,200,50);
    TestEqual(TEXT("nonuniform native slope"),Tri.Lower.SlopeDegrees,FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(4.25))),1.e-8);
    TestEqual(TEXT("central symmetric slope remains zero"),Analysis::SlopeFromHeights(100,100,100,100,50).SlopeDegrees,0.0);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04Immutable,"LandscapeMCP.V04.DryRunImmutablePreview",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04Immutable::RunTest(const FString& Parameters)
{
    UWorld* W=FAutomationEditorCommonUtils::CreateNewMap();if(!TestNotNull(TEXT("isolated world"),W))return false;
    const auto C=Create(W->PersistentLevel->GetPathName(),TEXT("V04Preview"),FVector::ZeroVector,FVector(100),1,1,1,63,0,false);
    if(!TestTrue(TEXT("existing create fixture"),C.bSuccess))return false;
    // fixture準備だけは既存v0.3 Sculptを使用。新規Toolはsourceを一切書かない。
    FEditRequest Peak;Peak.LandscapePath=C.LandscapePath;Peak.Center=FVector2D(3000,3000);Peak.RadiusCm=1;Peak.Strength=200;Peak.bDryRun=false;
    if(!TestTrue(TEXT("existing sculpt prepares peak fixture"),Edit(Peak).bSuccess))return false;
    AActor* Sentinel=W->SpawnActor<AActor>();const FTransform SentinelBefore=Sentinel->GetActorTransform();
    FHeightfieldSnapshot Before;FString Code,Error;
    if(!TestTrue(TEXT("source snapshot"),ReadSnapshot(C.LandscapePath,FVector2D(0,0),FVector2D(6300,6300),Before,Code,Error)))return false;
    W->GetOutermost()->SetDirtyFlag(false);const int32 UndoBefore=UToolsetLibrary::GetActiveUndoCount();
    int32 Saves=0;const auto SaveHandle=FCoreUObjectDelegates::OnObjectPreSave.AddLambda([&](UObject*,FObjectPreSaveContext){++Saves;});
    struct FGuard {FDelegateHandle H;~FGuard(){FCoreUObjectDelegates::OnObjectPreSave.Remove(H);}} Guard{SaveHandle};
    for(int32 Op=0;Op<3;++Op)
    {
        auto R=Op==1?RectangleRequest():PathRequest();R.LandscapePath=C.LandscapePath;
        if(Op==2){R.Operation=EAuthoringOperation::CreateWalkablePath;R.MaxSlope=30;}
        const double Started=FPlatformTime::Seconds();const auto O=Preview(R);const double Elapsed=(FPlatformTime::Seconds()-Started)*1000;
        TestTrue(*O.ErrorCode,O.bSuccess);TestEqual(TEXT("no writes"),O.ModifiedCount,0);
        FString PreviewJson;FJsonObjectConverter::UStructToJsonObjectString(O,PreviewJson);
        AddInfo(TEXT("PREVIEW ")+PreviewJson);
        AddInfo(FString::Printf(TEXT("PERFORMANCE bridge operation=%s readVertices=%d cachedSourceBytes=%lld elapsedMs=%.3f"),*O.Operation,O.ReadSampleCount,Before.CachedSourceBytes,Elapsed));
        R.bDryRun=false;TestEqual(TEXT("all apply disabled"),Preview(R).ErrorCode,FString(TEXT("APPLY_DISABLED_PHASE1")));
        R.bDryRun=true;R.MaxDelta=0;TestFalse(TEXT("rejection immutable"),Preview(R).bSuccess);
    }
    TestTrue(TEXT("facade preview"),ULandscapeMCPToolset::FlattenRectangle(C.LandscapePath,FVector2D(3000,3000),1000,800,100,300,300).bSuccess);
    FHeightfieldSnapshot After;TestTrue(TEXT("post snapshot"),ReadSnapshot(C.LandscapePath,FVector2D(0,0),FVector2D(6300,6300),After,Code,Error));
    TestTrue(TEXT("source bytes exactly unchanged"),Before.Heights==After.Heights);
    TestFalse(TEXT("no Package dirty"),W->GetOutermost()->IsDirty());TestEqual(TEXT("no Undo entries"),UToolsetLibrary::GetActiveUndoCount(),UndoBefore);
    TestFalse(TEXT("no active transaction"),GEditor->IsTransactionActive());TestEqual(TEXT("no saves"),Saves,0);
    TestTrue(TEXT("sentinel untouched"),SentinelBefore.Equals(Sentinel->GetActorTransform()));
    const auto Large=Create(W->PersistentLevel->GetPathName(),TEXT("V04Limit"),FVector(10000,0,0),FVector(25,25,100),3,3,1,63,0,false);
    if(!TestTrue(TEXT("large sample fixture"),Large.bSuccess))return false;
    FPreviewRequest Limit=RectangleRequest();Limit.LandscapePath=Large.LandscapePath;Limit.Center=FVector2D(12000,2000);Limit.SizeX=5000;Limit.SizeY=5000;
    TestEqual(TEXT("bridge read cap"),Preview(Limit).ErrorCode,FString(TEXT("SAMPLE_LIMIT_EXCEEDED")));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04Schema,"LandscapeMCP.V04.OfficialSchema13Tools",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04Schema::RunTest(const FString& Parameters)
{
    const FString Schema=UToolsetRegistry::GetToolsetJsonSchema(ULandscapeMCPToolset::StaticClass());
    TSharedPtr<FJsonObject> Current,Baseline;TestTrue(TEXT("current JSON"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Schema),Current));
    FString BaselineText;
    const FString Fixture=FPaths::ProjectPluginsDir()/TEXT("LandscapeMCP/Source/LandscapeMCP/Private/Tests/Fixtures/LandscapeMCP_v030_schema.json");
    if(!TestTrue(TEXT("official tag baseline fixture"),FFileHelper::LoadFileToString(BaselineText,*Fixture)))return false;
    if(!TestTrue(TEXT("baseline JSON"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BaselineText),Baseline)))return false;
    if(!Current.IsValid())return false;
    const auto& Tools=Current->GetArrayField(TEXT("tools"));TestEqual(TEXT("13 tools"),Tools.Num(),13);
    for(const auto& Old:Baseline->GetArrayField(TEXT("tools")))
    {
        bool Found=false;for(const auto& Tool:Tools)
            if(Tool->AsObject()->GetStringField(TEXT("name"))==Old->AsObject()->GetStringField(TEXT("name")))
            {
                Found=true;
                if(Old->AsObject()->GetStringField(TEXT("name")).EndsWith(TEXT("GetHeightRegion")))
                {
                    // Approved wording-only difference from the v0.3.0 tag: the version
                    // prefix of one output field description. Everything else stays frozen.
                    auto Field=[](const TSharedPtr<FJsonValue>& V){return V->AsObject()->GetObjectField(TEXT("outputSchema"))->GetObjectField(TEXT("properties"))
                        ->GetObjectField(TEXT("returnValue"))->GetObjectField(TEXT("properties"))->GetObjectField(TEXT("bTriangleSlopeValid"));};
                    const auto Before=Field(Old),After=Field(Tool);
                    TestEqual(TEXT("bTriangleSlopeValid description only lost its version prefix"),TEXT("v0.3: ")+After->GetStringField(TEXT("description")),Before->GetStringField(TEXT("description")));
                    Before->RemoveField(TEXT("description"));After->RemoveField(TEXT("description"));
                }
                TestTrue(*Old->AsObject()->GetStringField(TEXT("name")),FJsonValue::CompareEqual(*Old,*Tool));
            }
        TestTrue(TEXT("old tool retained"),Found);
    }
    TestEqual(TEXT("version"),Current->GetStringField(TEXT("version")),FString(TEXT("0.4")));
    for(const auto& Tool:Tools)
    {
        const auto Obj=Tool->AsObject();const FString N=Obj->GetStringField(TEXT("name"));
        if(!N.EndsWith(TEXT("FlattenPath"))&&!N.EndsWith(TEXT("FlattenRectangle"))&&!N.EndsWith(TEXT("CreateWalkablePath")))continue;
        const auto Input=Obj->GetObjectField(TEXT("inputSchema"));const auto Properties=Input->GetObjectField(TEXT("properties"));
        TestFalse(TEXT("no explicit Z profile"),Properties->HasField(TEXT("profileHeightsCm")));
        TestFalse(TEXT("no caller interval"),Properties->HasField(TEXT("profileSampleIntervalCm")));
        TestTrue(TEXT("safe dry run default"),Properties->GetObjectField(TEXT("bDryRun"))->GetBoolField(TEXT("default")));
        if(N.EndsWith(TEXT("FlattenRectangle")))TestEqual(TEXT("yaw default0"),Properties->GetObjectField(TEXT("yawDegrees"))->GetNumberField(TEXT("default")),0.0);
        const TArray<FString> Expected=N.EndsWith(TEXT("FlattenRectangle"))?
            TArray<FString>{TEXT("landscapePath"),TEXT("center"),TEXT("sizeXCm"),TEXT("sizeYCm"),TEXT("targetHeightCm"),TEXT("falloffCm"),TEXT("maxDeltaCm")}:
            N.EndsWith(TEXT("FlattenPath"))?
            TArray<FString>{TEXT("landscapePath"),TEXT("points"),TEXT("widthCm"),TEXT("falloffCm"),TEXT("heightMode"),TEXT("targetHeightCm"),TEXT("maxDeltaCm")}:
            TArray<FString>{TEXT("landscapePath"),TEXT("points"),TEXT("widthCm"),TEXT("falloffCm"),TEXT("maxSlopeDegrees"),TEXT("maxDeltaCm")};
        const auto& Required=Input->GetArrayField(TEXT("required"));TestEqual(TEXT("required argument count"),Required.Num(),Expected.Num());
        for(const FString& Field:Expected)
        {bool Found=false;for(const auto& Value:Required)Found|=Value->AsString()==Field;TestTrue(*Field,Found);}
        TestEqual(TEXT("frozen property count"),Properties->Values.Num(),Expected.Num()+(N.EndsWith(TEXT("FlattenRectangle"))?2:1));
    }
    AddInfo(Schema);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FV04Performance,"LandscapeMCP.V04.PerformanceLimits",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FV04Performance::RunTest(const FString& Parameters)
{
    auto S=Snapshot(128,FVector(100));auto R=RectangleRequest();R.Center=FVector2D(6000,6000);R.SizeX=5000;R.SizeY=5000;R.Falloff=2500;
    double Worst=0;for(int32 I=0;I<5;++I)
    {const double Start=FPlatformTime::Seconds();const auto O=PlanPreview(R,S);Worst=FMath::Max(Worst,(FPlatformTime::Seconds()-Start)*1000);TestTrue(*O.ErrorCode,O.bSuccess);}
    AddInfo(FString::Printf(TEXT("PERFORMANCE rectangle snapshotVertices=%d worstMs=%.3f"),S.Heights.Num(),Worst));
    S=Snapshot(128,FVector(50));R=PathRequest();R.Points={FVector2D(1200,3200),FVector2D(5000,3200)};R.Width=400;R.Falloff=200;
    const double Start=FPlatformTime::Seconds();const auto O=PlanPreview(R,S);const double Millis=(FPlatformTime::Seconds()-Start)*1000;
    TestTrue(*O.ErrorCode,O.bSuccess);AddInfo(FString::Printf(TEXT("PERFORMANCE path snapshotVertices=%d profile=%d triangles=%d elapsedMs=%.3f"),S.Heights.Num(),O.Path.ProfileSampleCount,O.PlannedAfter.TriangleCount,Millis));
    S.Origin=FVector::ZeroVector;S.Scale=FVector(1,1,100);S.Rect=S.Extent=FIntRect(0,0,1026,7);S.Heights.Init(32768,1027*8);
    R=PathRequest();R.Width=3;R.Falloff=0.5;R.Points.Reset();for(int32 I=0;I<64;++I)R.Points.Add(FVector2D(10+I*16,4));
    const double DenseStart=FPlatformTime::Seconds();const auto Dense=PlanPreview(R,S);
    TestTrue(*Dense.ErrorCode,Dense.bSuccess);AddInfo(FString::Printf(TEXT("PERFORMANCE maxPoints=64 snapshotVertices=%d profile=%d elapsedMs=%.3f"),S.Heights.Num(),Dense.Path.ProfileSampleCount,(FPlatformTime::Seconds()-DenseStart)*1000));
    R.Points={FVector2D(1,4),FVector2D(1024,4)};const double CapStart=FPlatformTime::Seconds();const auto Cap=PlanPreview(R,S);
    TestTrue(*Cap.ErrorCode,Cap.bSuccess);TestEqual(TEXT("profile1024"),Cap.Path.ProfileSampleCount,1024);
    AddInfo(FString::Printf(TEXT("PERFORMANCE maxProfile=1024 snapshotVertices=%d elapsedMs=%.3f"),S.Heights.Num(),(FPlatformTime::Seconds()-CapStart)*1000));
    // 計測値を隠す上限調整や時間に依存する不安定なassertionは行わない。
    return true;
}
#endif
