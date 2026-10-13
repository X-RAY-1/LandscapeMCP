#include "LandscapeMCPTerrainPlanning.h"
#include "LandscapeMCPTerrainAnalysis.h"

namespace LandscapeMCP::Authoring
{
namespace
{
    bool Coord(double V) {return FMath::IsFinite(V)&&FMath::Abs(V)<=10000000;}
    FString Name(EAuthoringOperation Op)
    {
        if(Op==EAuthoringOperation::FlattenRectangle)return TEXT("FlattenRectangle");
        return Op==EAuthoringOperation::FlattenPath?TEXT("FlattenPath"):TEXT("CreateWalkablePath");
    }
    FLandscapeMCPTerrainOperationResult Initial(const FPreviewRequest& R)
    {
        FLandscapeMCPTerrainOperationResult O;O.Operation=Name(R.Operation);O.LandscapePath=R.LandscapePath;O.bDryRun=R.bDryRun;return O;
    }
    FLandscapeMCPTerrainOperationResult Reject(FLandscapeMCPTerrainOperationResult O,const FString& Code,const FString& Detail=FString())
    {
        O.bSuccess=false;O.bPlanValid=false;O.bWalkabilityGuaranteed=false;O.Status=TEXT("REJECTED");O.ErrorCode=Code;
        if(O.Path.ProfileSamples.Num()!=O.Path.ProfileSampleCount)O.Path=FLandscapeMCPPathPreview();
        O.Message=Detail.IsEmpty()?Code:Detail;
        if(Code.StartsWith(TEXT("UNSUPPORTED"))||Code==TEXT("FOLIAGE_SAFETY_CONFLICT"))O.UnsupportedReasons.Add(Code+TEXT(": ")+O.Message);
        return O;
    }
    FLandscapeMCPBounds Bounds(FVector2D Min,FVector2D Max)
    {FLandscapeMCPBounds B;B.bValid=true;B.Min=Min;B.Max=Max;return B;}
    FVector2D World(const FHeightfieldSnapshot& S,int32 X,int32 Y)
    {return FVector2D(S.Origin.X+X*S.Scale.X,S.Origin.Y+Y*S.Scale.Y);}
    double Outside(const FPreviewRequest& R,const FPath& P,FVector2D V)
    {
        if(R.Operation!=EAuthoringOperation::FlattenRectangle)return PathDistance(P,V)-R.Width/2;
        const double X=FMath::Max(0.0,FMath::Abs(V.X-R.Center.X)-R.SizeX/2);
        const double Y=FMath::Max(0.0,FMath::Abs(V.Y-R.Center.Y)-R.SizeY/2);
        return FMath::Sqrt(X*X+Y*Y);
    }
    bool Candidate(const FPreviewRequest& R,const FPath& P,FVector2D V)
    {
        const double D=Outside(R,P,V);
        return R.Falloff==0?D<=0:D<R.Falloff;
    }
    FLandscapeMCPTerrainStats Stats(const FHeightfieldSnapshot& S,TConstArrayView<uint16> Raw,
        const TArray<uint8>& Vertices,const TArray<uint8>& Triangles,bool Walkable,double Threshold)
    {
        FLandscapeMCPTerrainStats O;TArray<double> Heights;Heights.Reserve(Raw.Num());
        double Sum=0;int32 N=0;
        for(int32 I=0;I<Raw.Num();++I)
        {
            const double Z=S.Decode(Raw[I]);Heights.Add(Z);
            if(Vertices[I]) {if(N==0)O.MinHeightCm=O.MaxHeightCm=Z;else {O.MinHeightCm=FMath::Min(O.MinHeightCm,Z);O.MaxHeightCm=FMath::Max(O.MaxHeightCm,Z);}Sum+=Z;++N;}
        }
        O.bValid=N>0;O.MeanHeightCm=N?Sum/N:0;
        const auto T=Analysis::ComputeTriangleStats(Heights,S.CountX(),S.CountY(),S.Scale.X,S.Scale.Y,
            Walkable?Threshold:90,S.Scale.Z/128,[&](int32 X,int32 Y,bool Upper){return Triangles[(Y*(S.CountX()-1)+X)*2+(Upper?1:0)]!=0;});
        O.TriangleCount=T.TriangleCount;O.bSlopeValid=T.TriangleCount>0;O.MaxTriangleSlopeDegrees=T.MaxSlopeDegrees;
        O.MaxSlopeUncertaintyDegrees=T.MaxSlopeUncertaintyDegrees;
        O.bWalkabilityValid=Walkable&&T.TriangleCount>0;
        if(O.bWalkabilityValid)
        {
            O.WalkableFloorAngleDeg=Threshold;O.WalkableCount=T.WalkableCount;O.NearLimitCount=T.NearLimitCount;
            O.UnwalkableCount=T.UnwalkableCount;O.WalkableRatio=double(T.WalkableCount)/T.TriangleCount;O.WorstMarginDegrees=Threshold-T.MaxSlopeDegrees;
        }
        return O;
    }
}
bool FHeightfieldSnapshot::Sample(FVector2D V,TConstArrayView<uint16> Data,double& Height) const
{
    if(Scale.X<=0||Scale.Y<=0||Data.Num()!=CountX()*CountY())return false;
    const double X=(V.X-Origin.X)/Scale.X,Y=(V.Y-Origin.Y)/Scale.Y;
    if(!FMath::IsFinite(X)||!FMath::IsFinite(Y)||X<Rect.Min.X||X>Rect.Max.X||Y<Rect.Min.Y||Y>Rect.Max.Y)return false;
    const int32 X0=FMath::FloorToInt(X),Y0=FMath::FloorToInt(Y),X1=FMath::Min(X0+1,Rect.Max.X),Y1=FMath::Min(Y0+1,Rect.Max.Y);
    auto At=[&](int32 IX,int32 IY){return double(Data[(IY-Rect.Min.Y)*CountX()+IX-Rect.Min.X]);};
    const double Encoded=FMath::Lerp(FMath::Lerp(At(X0,Y0),At(X1,Y0),X-X0),FMath::Lerp(At(X0,Y1),At(X1,Y1),X-X0),Y-Y0);
    Height=Origin.Z+(Encoded-32768.0)*Scale.Z/128.0;return FMath::IsFinite(Height);
}
bool ValidateRequest(const FPreviewRequest& R,FPath& P,FVector2D& Min,FVector2D& Max,FString& Code)
{
    // apply要求はvalidation/readより先に拒否。writeへ到達するinterfaceはない。
    if(!R.bDryRun){Code=TEXT("APPLY_DISABLED_PHASE1");return false;}
    if(!FMath::IsFinite(R.Falloff)||R.Falloff<0||R.Falloff>2500||!FMath::IsFinite(R.MaxDelta)||R.MaxDelta<=0||R.MaxDelta>1000)
    {Code=TEXT("INVALID_DIMENSIONS");return false;}
    if(R.Operation==EAuthoringOperation::FlattenRectangle)
    {
        if(!FMath::IsFinite(R.Yaw)||R.Yaw!=0){Code=TEXT("UNSUPPORTED_RECTANGLE_YAW");return false;}
        if(!Coord(R.Center.X)||!Coord(R.Center.Y)||!Coord(R.TargetHeight)||!FMath::IsFinite(R.SizeX)||!FMath::IsFinite(R.SizeY)||R.SizeX<=0||R.SizeY<=0||R.SizeX>5000||R.SizeY>5000)
        {Code=TEXT("INVALID_DIMENSIONS");return false;}
        Min=R.Center-FVector2D(R.SizeX/2+R.Falloff,R.SizeY/2+R.Falloff);Max=R.Center+FVector2D(R.SizeX/2+R.Falloff,R.SizeY/2+R.Falloff);
    }
    else
    {
        if(!FMath::IsFinite(R.Width)||R.Width<=0||R.Width>5000){Code=TEXT("INVALID_DIMENSIONS");return false;}
        if(R.Operation==EAuthoringOperation::FlattenPath)
        {
            if(R.HeightMode!=TEXT("fixed")&&R.HeightMode!=TEXT("follow_start")&&R.HeightMode!=TEXT("follow_path")) {Code=TEXT("INVALID_HEIGHT_MODE");return false;}
            // 非fixedでは値を使用しないが、NaN/Infは入力として受理しない。
            if(!Coord(R.TargetHeight)){Code=TEXT("INVALID_COORDINATE");return false;}
        }
        else if(!FMath::IsFinite(R.MaxSlope)||R.MaxSlope<=0||R.MaxSlope>60){Code=TEXT("INVALID_SLOPE");return false;}
        const double Radius=R.Width/2+R.Falloff;
        if(!ValidatePath(R.Points,Radius,P,Code))return false;
        Min=Max=P.Points[0];
        for(FVector2D V:P.Points){Min.X=FMath::Min(Min.X,V.X);Min.Y=FMath::Min(Min.Y,V.Y);Max.X=FMath::Max(Max.X,V.X);Max.Y=FMath::Max(Max.Y,V.Y);}
        Min-=FVector2D(Radius,Radius);Max+=FVector2D(Radius,Radius);
    }
    if(!Coord(Min.X)||!Coord(Min.Y)||!Coord(Max.X)||!Coord(Max.Y)){Code=TEXT("INVALID_COORDINATE");return false;}
    return true;
}
bool PlanWalkableProfile(TConstArrayView<FStation> S,TConstArrayView<double> H,double Delta,double Angle,double MinZ,double MaxZ,TArray<double>& Target)
{
    Target.Reset();if(S.Num()<2||S.Num()!=H.Num())return false;
    TArray<double> Lo,Hi;Lo.SetNum(H.Num());Hi.SetNum(H.Num());
    const double Grade=FMath::Tan(FMath::DegreesToRadians(Angle));
    for(int32 I=0;I<H.Num();++I){Lo[I]=FMath::Max(MinZ,H[I]-Delta);Hi[I]=FMath::Min(MaxZ,H[I]+Delta);}
    Lo[0]=Hi[0]=H[0];Lo.Last()=Hi.Last()=H.Last();
    for(int32 I=1;I<H.Num();++I)
    {
        const double Reach=Grade*(S[I].Distance-S[I-1].Distance);
        Lo[I]=FMath::Max(Lo[I],Lo[I-1]-Reach);Hi[I]=FMath::Min(Hi[I],Hi[I-1]+Reach);if(Lo[I]>Hi[I])return false;
    }
    for(int32 I=H.Num()-2;I>=0;--I)
    {
        const double Reach=Grade*(S[I+1].Distance-S[I].Distance);
        Lo[I]=FMath::Max(Lo[I],Lo[I+1]-Reach);Hi[I]=FMath::Min(Hi[I],Hi[I+1]+Reach);if(Lo[I]>Hi[I])return false;
    }
    Target.Add(H[0]);
    for(int32 I=1;I<H.Num();++I)
    {
        const double Reach=Grade*(S[I].Distance-S[I-1].Distance);
        const double L=FMath::Max(Lo[I],Target.Last()-Reach),U=FMath::Min(Hi[I],Target.Last()+Reach);
        if(L>U)return false;Target.Add(FMath::Clamp(H[I],L,U));
    }
    return true;
}
FLandscapeMCPTerrainOperationResult Preview(const FPreviewRequest& R,FString* OutFailureStage)
{
    auto O=Initial(R);FPath P;FVector2D Min,Max;FString Code,Error;
    auto Stage=[&](const TCHAR* S){if(OutFailureStage)*OutFailureStage=S;};
    if(!ValidateRequest(R,P,Min,Max,Code)){Stage(TEXT("validation"));return Reject(O,Code);}
    FHeightfieldSnapshot S;
    if(!ReadSnapshot(R.LandscapePath,Min,Max,S,Code,Error)){Stage(TEXT("snapshot"));return Reject(O,Code,Error);}
    O=PlanPreview(R,S);if(!O.bSuccess)Stage(TEXT("planning"));
    return O;
}
FLandscapeMCPTerrainOperationResult PlanPreview(const FPreviewRequest& R,const FHeightfieldSnapshot& S,FPreparedTerrainPlan* Prepared)
{
    if(Prepared)*Prepared={};
    auto O=Initial(R);FPath P;FVector2D Min,Max;FString Code;
    if(!ValidateRequest(R,P,Min,Max,Code))return Reject(O,Code);
    if(S.Heights.Num()<4||S.CountX()<2||S.CountY()<2||S.Heights.Num()!=int64(S.CountX())*S.CountY()||S.Heights.Num()>MaxReadVertices
        ||S.CachedSourceBytes>MaxCachedSourceBytes||!Coord(S.Origin.X)||!Coord(S.Origin.Y)||!Coord(S.Origin.Z)
        ||!FMath::IsFinite(S.Scale.X)||!FMath::IsFinite(S.Scale.Y)||!FMath::IsFinite(S.Scale.Z)||S.Scale.GetMin()<1||S.Scale.GetMax()>1000)
        return Reject(O,TEXT("INVALID_SNAPSHOT"));
    const bool Rect=R.Operation==EAuthoringOperation::FlattenRectangle,Walk=R.Operation==EAuthoringOperation::CreateWalkablePath;
    O.HeightQuantizationCm=S.Scale.Z/128;
    const FVector2D ExtMin=World(S,S.Extent.Min.X,S.Extent.Min.Y),ExtMax=World(S,S.Extent.Max.X,S.Extent.Max.Y);
    auto Contains=[&](FVector2D V){return V.X>=ExtMin.X&&V.Y>=ExtMin.Y&&V.X<=ExtMax.X&&V.Y<=ExtMax.Y;};
    if(Rect){if(!Contains(R.Center))return Reject(O,TEXT("OUTSIDE_LANDSCAPE"));}
    else for(FVector2D V:P.Points)if(!Contains(V))return Reject(O,TEXT("OUTSIDE_LANDSCAPE"));
    const double Radius=R.Width/2+R.Falloff;
    if(Walk)
        for(FVector2D V:P.Points)
            if(!Contains(V-FVector2D(R.Width/2,R.Width/2))||!Contains(V+FVector2D(R.Width/2,R.Width/2)))
                return Reject(O,TEXT("INCOMPLETE_WALKABLE_CORRIDOR"));
    O.RequestedBounds=Bounds(Min,Max);O.ReadBounds=Bounds(World(S,S.Rect.Min.X,S.Rect.Min.Y),World(S,S.Rect.Max.X,S.Rect.Max.Y));
    O.ReadSampleCount=S.Heights.Num();
    O.bClipped=Min.X<ExtMin.X||Min.Y<ExtMin.Y||Max.X>ExtMax.X||Max.Y>ExtMax.Y;
    if(O.bClipped)O.Warnings.Add(TEXT("BOUNDS_CLIPPED: core or falloff clipped to Landscape; see affected bounds."));
    const int32 X0=FMath::FloorToInt((Min.X-S.Origin.X)/S.Scale.X),Y0=FMath::FloorToInt((Min.Y-S.Origin.Y)/S.Scale.Y);
    const int32 X1=FMath::CeilToInt((Max.X-S.Origin.X)/S.Scale.X),Y1=FMath::CeilToInt((Max.Y-S.Origin.Y)/S.Scale.Y);
    if((int64(X1)-X0+1)*(int64(Y1)-Y0+1)>MaxAabbCandidates)return Reject(O,TEXT("SAMPLE_LIMIT_EXCEEDED"));
    // snapshotの切り落とされた部分を見ずに安全判定しない。全supportと1セルhaloが必要。
    if(S.Rect.Min.X>FMath::Max(S.Extent.Min.X,X0-1)||S.Rect.Min.Y>FMath::Max(S.Extent.Min.Y,Y0-1)
        ||S.Rect.Max.X<FMath::Min(S.Extent.Max.X,X1+1)||S.Rect.Max.Y<FMath::Min(S.Extent.Max.Y,Y1+1))
        return Reject(O,TEXT("INCOMPLETE_SNAPSHOT"));
    for(int32 Y=Y0;Y<=Y1;++Y)for(int32 X=X0;X<=X1;++X)
        if(Candidate(R,P,World(S,X,Y)))++O.RequestedSampleCount;
    if(O.RequestedSampleCount>MaxRequestedVertices)return Reject(O,TEXT("SAMPLE_LIMIT_EXCEEDED"));
    TArray<FStation> Stations;TArray<double> Current,Target;
    if(!Rect)
    {
        // 原格子より粗化しない。1024に収めるためのinterval拡大は禁止。
        const double Interval=FMath::Min(100.0,FMath::Min(S.Scale.X,S.Scale.Y));
        if(!SamplePath(P,Interval,Stations,Code))return Reject(O,Code);
        for(const FStation& Station:Stations){double Z;if(!S.Sample(Station.Position,S.Heights,Z))return Reject(O,TEXT("INVALID_SNAPSHOT"));Current.Add(Z);}
        if(Walk)
        {
            const double Error=Analysis::TriangleGradientError(S.Scale.Z/128,S.Scale.X,S.Scale.Y);
            const double PlanningAngle=R.MaxSlope-2*FMath::RadiansToDegrees(FMath::Atan(Error));
            if(PlanningAngle<=0||!PlanWalkableProfile(Stations,Current,R.MaxDelta,PlanningAngle,S.Decode(0),S.Decode(65535),Target))return Reject(O,TEXT("CANNOT_SATISFY_WALKABILITY"));
        }
        else if(R.HeightMode==TEXT("follow_path"))
        {
            TArray<FStation> Knots;TArray<double> KnotHeights;
            for(int32 I=0;I<P.Points.Num();++I){double Z;if(!S.Sample(P.Points[I],S.Heights,Z))return Reject(O,TEXT("INVALID_SNAPSHOT"));Knots.Add({P.Points[I],P.Distances[I]});KnotHeights.Add(Z);}
            for(const FStation& Station:Stations)Target.Add(ProfileAt(Knots,KnotHeights,Station.Distance));
        }
        else Target.Init(R.HeightMode==TEXT("fixed")?R.TargetHeight:Current[0],Stations.Num());
        O.Path.bValid=true;O.Path.PathLengthCm=P.Length;O.Path.PathWidthCm=R.Width;O.Path.FalloffCm=R.Falloff;
        for(int32 I=1;I<Stations.Num();++I)O.Path.ProfileSamplingIntervalCm=FMath::Max(O.Path.ProfileSamplingIntervalCm,Stations[I].Distance-Stations[I-1].Distance);
        O.Path.ProfileSampleCount=Stations.Num();
    }
    // constant targetのencode可能性も検証。falloffが小さいから範囲外targetを許可しない。
    uint16 Dummy;
    if((Rect||(!Walk&&R.HeightMode==TEXT("fixed")))&&!EncodeHeight(R.TargetHeight,S.Origin.Z,S.Scale.Z,Dummy))return Reject(O,TEXT("HEIGHT_OUT_OF_RANGE"));
    TArray<uint16> Planned=S.Heights;TArray<uint8> Affected,Changed;Affected.Init(0,Planned.Num());Changed.Init(0,Planned.Num());
    bool HasBounds=false;FVector2D AMin,AMax;
    int32 CoreVertexCount=0,CoreMinX=MAX_int32,CoreMaxX=MIN_int32,CoreMinY=MAX_int32,CoreMaxY=MIN_int32;
    for(int32 Y=S.Rect.Min.Y;Y<=S.Rect.Max.Y;++Y)for(int32 X=S.Rect.Min.X;X<=S.Rect.Max.X;++X)
    {
        const int32 I=(Y-S.Rect.Min.Y)*S.CountX()+X-S.Rect.Min.X;const FVector2D V=World(S,X,Y);
        if(!Candidate(R,P,V))continue;
        Affected[I]=1;++O.AffectedSampleCount;
        if(!HasBounds){AMin=AMax=V;HasBounds=true;}else{AMin.X=FMath::Min(AMin.X,V.X);AMin.Y=FMath::Min(AMin.Y,V.Y);AMax.X=FMath::Max(AMax.X,V.X);AMax.Y=FMath::Max(AMax.Y,V.Y);}
        if(Outside(R,P,V)<=0){++CoreVertexCount;CoreMinX=FMath::Min(CoreMinX,X);CoreMaxX=FMath::Max(CoreMaxX,X);CoreMinY=FMath::Min(CoreMinY,Y);CoreMaxY=FMath::Max(CoreMaxY,Y);}
        const double Before=S.Decode(S.Heights[I]),Z=Rect?R.TargetHeight:JoinedHeight(P,Stations,Target,V,Radius);
        const double Next=FMath::Lerp(Before,Z,FalloffWeight(Outside(R,P,V),R.Falloff));
        if(FMath::Abs(Next-Before)>R.MaxDelta)return Reject(O,TEXT("MAX_DELTA_EXCEEDED"));
        if(!EncodeHeight(Next,S.Origin.Z,S.Scale.Z,Planned[I]))return Reject(O,TEXT("HEIGHT_OUT_OF_RANGE"));
        const double Delta=S.Decode(Planned[I])-Before;
        if(FMath::Abs(Delta)>R.MaxDelta)return Reject(O,TEXT("MAX_DELTA_EXCEEDED"));
        if(O.AffectedSampleCount==1)O.Delta.MinDeltaCm=O.Delta.MaxDeltaCm=Delta;
        else {O.Delta.MinDeltaCm=FMath::Min(O.Delta.MinDeltaCm,Delta);O.Delta.MaxDeltaCm=FMath::Max(O.Delta.MaxDeltaCm,Delta);}
        if(Planned[I]!=S.Heights[I]){Changed[I]=1;++O.PlannedModifiedCount;}
    }
    if(!HasBounds||CoreVertexCount<4||CoreMinX==CoreMaxX||CoreMinY==CoreMaxY)return Reject(O,TEXT("INSUFFICIENT_SAMPLES"));
    // 各stationでcoreに2Dの格子支持があることを保守的に検証。
    if(!Rect)for(const FStation& Station:Stations)
    {
        const int32 GX=FMath::FloorToInt((Station.Position.X-S.Origin.X)/S.Scale.X),GY=FMath::FloorToInt((Station.Position.Y-S.Origin.Y)/S.Scale.Y);
        for(int32 DY=0;DY<=1;++DY)for(int32 DX=0;DX<=1;++DX)
            if(!Contains(World(S,GX+DX,GY+DY))||Outside(R,P,World(S,GX+DX,GY+DY))>0)return Reject(O,TEXT("INSUFFICIENT_SAMPLES"));
    }
    O.AffectedBounds=Bounds(AMin,AMax);O.ClippedSampleCount=O.RequestedSampleCount-O.AffectedSampleCount;
    O.Delta.bValid=true;O.Delta.MaxRaiseCm=FMath::Max(0.0,O.Delta.MaxDeltaCm);O.Delta.MaxLowerCm=FMath::Max(0.0,-O.Delta.MinDeltaCm);
    const int32 CellCount=(S.CountX()-1)*(S.CountY()-1);TArray<uint8> Core,Transition;Core.Init(0,2*CellCount);Transition.Init(0,2*CellCount);
    bool HasVerify=false;FVector2D VMin,VMax;
    for(int32 Y=0;Y<S.CountY()-1;++Y)for(int32 X=0;X<S.CountX()-1;++X)for(int32 U=0;U<2;++U)
    {
        const int32 I=Y*S.CountX()+X,J=U?I+S.CountX():I+1,K=I+S.CountX()+1,T=(Y*(S.CountX()-1)+X)*2+U;
        const FVector2D A=World(S,S.Rect.Min.X+X,S.Rect.Min.Y+Y),B=World(S,S.Rect.Min.X+X+(U?0:1),S.Rect.Min.Y+Y+(U?1:0)),C=World(S,S.Rect.Min.X+X+1,S.Rect.Min.Y+Y+1);
        const EIntersection Inter=Rect?TriangleRectangle(A,B,C,R.Center-FVector2D(R.SizeX/2,R.SizeY/2),R.Center+FVector2D(R.SizeX/2,R.SizeY/2),FVector2D(S.Scale.X,S.Scale.Y)):
            TrianglePath(A,B,C,P,R.Width/2,FVector2D(S.Scale.X,S.Scale.Y));
        if(Inter==EIntersection::Ambiguous)return Reject(O,TEXT("AMBIGUOUS_CORE_INTERSECTION"));
        Core[T]=Inter==EIntersection::Positive;
        Transition[T]=!Core[T]&&(Affected[I]||Affected[J]||Affected[K]||Changed[I]||Changed[J]||Changed[K]);
        if(Core[T]||Transition[T])for(FVector2D V:{A,B,C})
        {
            if(!HasVerify){VMin=VMax=V;HasVerify=true;}else{VMin.X=FMath::Min(VMin.X,V.X);VMin.Y=FMath::Min(VMin.Y,V.Y);VMax.X=FMath::Max(VMax.X,V.X);VMax.Y=FMath::Max(VMax.Y,V.Y);}
        }
        if(Walk&&Transition[T])
        {
            const auto Old=Analysis::CellTriangleSlopes(S.Decode(S.Heights[I]),S.Decode(S.Heights[I+1]),S.Decode(S.Heights[I+S.CountX()]),S.Decode(S.Heights[K]),S.Scale.X,S.Scale.Y);
            const auto New=Analysis::CellTriangleSlopes(S.Decode(Planned[I]),S.Decode(Planned[I+1]),S.Decode(Planned[I+S.CountX()]),S.Decode(Planned[K]),S.Scale.X,S.Scale.Y);
            if((U?New.Upper:New.Lower).SlopeDegrees>FMath::Max((U?Old.Upper:Old.Lower).SlopeDegrees,R.MaxSlope)+1.e-9)return Reject(O,TEXT("UNSAFE_TRANSITION"));
        }
    }
    O.VerificationBounds=HasVerify?Bounds(VMin,VMax):FLandscapeMCPBounds();
    O.Before=Stats(S,S.Heights,Affected,Core,Walk,R.MaxSlope);O.PlannedAfter=Stats(S,Planned,Affected,Core,Walk,R.MaxSlope);
    O.TransitionBefore=Stats(S,S.Heights,Affected,Transition,Walk,R.MaxSlope);O.TransitionPlannedAfter=Stats(S,Planned,Affected,Transition,Walk,R.MaxSlope);
    if(!O.Before.bSlopeValid||!O.PlannedAfter.bSlopeValid)return Reject(O,TEXT("INSUFFICIENT_SAMPLES"));
    if(Walk&&(O.PlannedAfter.WalkableRatio!=1||O.PlannedAfter.NearLimitCount!=0||O.PlannedAfter.UnwalkableCount!=0))return Reject(O,TEXT("CANNOT_SATISFY_WALKABILITY"));
    if(!Rect)
    {
        for(int32 I=0;I<Stations.Num();++I)
        {
            double Z;if(!S.Sample(Stations[I].Position,Planned,Z))return Reject(O,TEXT("INVALID_SNAPSHOT"));
            FLandscapeMCPProfileSample V;V.DistanceCm=Stations[I].Distance;V.WorldX=Stations[I].Position.X;V.WorldY=Stations[I].Position.Y;
            V.CurrentHeightCm=Current[I];V.TargetHeightCm=Target[I];V.PlannedHeightCm=Z;O.Path.ProfileSamples.Add(V);
        }
        if(Walk&&(FMath::Abs(O.Path.ProfileSamples[0].PlannedHeightCm-Current[0])>O.HeightQuantizationCm/2+1.e-9
            ||FMath::Abs(O.Path.ProfileSamples.Last().PlannedHeightCm-Current.Last())>O.HeightQuantizationCm/2+1.e-9))return Reject(O,TEXT("CANNOT_SATISFY_WALKABILITY"));
    }
    if(!Walk)O.Warnings.Add(TEXT("NO_WALKABILITY_GUARANTEE: flattening has no caller supplied slope threshold."));
    O.Warnings.Add(TEXT("PREVIEW_ONLY: no Landscape write. A successful preview does not guarantee that Apply is accepted; Apply performs additional runtime safety checks. Collision and Character behavior are not verified by a preview."));
    O.bSuccess=true;O.bPlanValid=true;O.bWalkabilityGuaranteed=Walk;O.Status=O.PlannedModifiedCount?TEXT("PREVIEW_READY"):TEXT("NO_CHANGE");
    O.Message=TEXT("Quantized source heightfield plan verified. Preview only; no transaction or save.");
    if(Prepared){Prepared->Quantized=MoveTemp(Planned);Prepared->Affected=MoveTemp(Affected);Prepared->Core=MoveTemp(Core);Prepared->Transition=MoveTemp(Transition);}
    return O;
}
bool VerifyPreparedGeometry(const FPreviewRequest& R,const FHeightfieldSnapshot& Original,
    const FHeightfieldSnapshot& Actual,const FPreparedTerrainPlan& P,FLandscapeMCPTerrainOperationResult& O)
{
    if(Actual.Rect!=Original.Rect||Actual.Extent!=Original.Extent||Actual.Origin!=Original.Origin||Actual.Scale!=Original.Scale
        ||Actual.Heights!=P.Quantized||P.Affected.Num()!=Actual.Heights.Num()
        ||P.Core.Num()!=2*(Actual.CountX()-1)*(Actual.CountY()-1)||P.Transition.Num()!=P.Core.Num())return false;
    const bool Walk=R.Operation==EAuthoringOperation::CreateWalkablePath;
    O.ActualAfter=Stats(Actual,Actual.Heights,P.Affected,P.Core,Walk,R.MaxSlope);
    O.TransitionActualAfter=Stats(Actual,Actual.Heights,P.Affected,P.Transition,Walk,R.MaxSlope);
    if(!O.ActualAfter.bValid||!O.ActualAfter.bSlopeValid)return false;
    if(Walk&&(O.ActualAfter.WalkableRatio!=1||O.ActualAfter.NearLimitCount||O.ActualAfter.UnwalkableCount))return false;
    for(int32 T=0;Walk&&T<P.Transition.Num();++T)if(P.Transition[T])
    {
        const int32 Cell=T/2,X=Cell%(Actual.CountX()-1),Y=Cell/(Actual.CountX()-1),I=Y*Actual.CountX()+X;
        auto Slopes=[&](const FHeightfieldSnapshot& S){return Analysis::CellTriangleSlopes(S.Decode(S.Heights[I]),S.Decode(S.Heights[I+1]),S.Decode(S.Heights[I+S.CountX()]),S.Decode(S.Heights[I+S.CountX()+1]),S.Scale.X,S.Scale.Y);};
        const auto B=Slopes(Original),A=Slopes(Actual);
        if((T%2?A.Upper:A.Lower).SlopeDegrees>FMath::Max((T%2?B.Upper:B.Lower).SlopeDegrees,R.MaxSlope)+1.e-9)return false;
    }
    for(int32 I=0;I<O.Path.ProfileSamples.Num();++I)
    {
        const auto& Sample=O.Path.ProfileSamples[I];double Z;
        if(!Actual.Sample(FVector2D(Sample.WorldX,Sample.WorldY),Actual.Heights,Z)||FMath::Abs(Z-Sample.PlannedHeightCm)>1.e-9)return false;
        if(Walk&&(I==0||I==O.Path.ProfileSamples.Num()-1)&&FMath::Abs(Z-Sample.CurrentHeightCm)>O.HeightQuantizationCm/2+1.e-9)return false;
    }
    O.ModifiedCount=0;for(int32 I=0;I<Actual.Heights.Num();++I)O.ModifiedCount+=Actual.Heights[I]!=Original.Heights[I];
    O.bActualValid=true;return true;
}
}
