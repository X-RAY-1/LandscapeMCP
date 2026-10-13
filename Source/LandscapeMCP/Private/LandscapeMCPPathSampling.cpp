#include "LandscapeMCPPathSampling.h"

namespace LandscapeMCP::Authoring
{
namespace
{
    double Cross(FVector2D A, FVector2D B) { return A.X*B.Y-A.Y*B.X; }
    bool InTriangle(FVector2D P, FVector2D A, FVector2D B, FVector2D C)
    {
        const double X=Cross(B-A,P-A),Y=Cross(C-B,P-B),Z=Cross(A-C,P-C);
        return (X>=0 && Y>=0 && Z>=0)||(X<=0 && Y<=0 && Z<=0);
    }
    double PointSegment(FVector2D P,FVector2D A,FVector2D B)
    { return FVector2D::Distance(P,FMath::Lerp(A,B,SegmentProjection(P,A,B))); }
}
double SegmentProjection(FVector2D P,FVector2D A,FVector2D B)
{
    const double D=(B-A).SizeSquared();
    return D>0 ? FMath::Clamp(FVector2D::DotProduct(P-A,B-A)/D,0.0,1.0):0;
}
double SegmentDistance(FVector2D A,FVector2D B,FVector2D C,FVector2D D)
{
    const FVector2D U=B-A,V=D-C;
    const double Den=Cross(U,V);
    if (Den!=0)
    {
        const double T=Cross(C-A,V)/Den,S=Cross(C-A,U)/Den;
        if(T>=0 && T<=1 && S>=0 && S<=1) return 0;
    }
    return FMath::Min(FMath::Min(PointSegment(A,C,D),PointSegment(B,C,D)),FMath::Min(PointSegment(C,A,B),PointSegment(D,A,B)));
}
bool ValidatePath(TConstArrayView<FVector2D> Points,double Radius,FPath& Out,FString& Code)
{
    Out={};
    if(Points.Num()<2 || Points.Num()>MaxPathPoints) { Code=TEXT("INVALID_PATH"); return false; }
    for(FVector2D P:Points)
        if(!FMath::IsFinite(P.X)||!FMath::IsFinite(P.Y)||FMath::Abs(P.X)>10000000||FMath::Abs(P.Y)>10000000)
        { Code=TEXT("INVALID_COORDINATE");return false; }
    Out.Points.Append(Points.GetData(),Points.Num()); Out.Distances.Add(0);
    for(int32 I=1;I<Points.Num();++I)
    {
        const double L=FVector2D::Distance(Points[I-1],Points[I]);
        if(L<1) {Code=TEXT("DEGENERATE_SEGMENT");return false;}
        Out.Length+=L;Out.Distances.Add(Out.Length);
        if(I>1 && FVector2D::DotProduct(Points[I-1]-Points[I-2],Points[I]-Points[I-1])<=0)
        {Code=TEXT("AMBIGUOUS_PROFILE");return false;}
    }
    if(Out.Length>10000) {Code=TEXT("REQUEST_TOO_LARGE");return false;}
    for(int32 I=0;I+1<Points.Num();++I)
        for(int32 J=I+2;J+1<Points.Num();++J)
            if(SegmentDistance(Points[I],Points[I+1],Points[J],Points[J+1])<=2*Radius)
            {Code=TEXT("AMBIGUOUS_PROFILE");return false;}
    return true;
}
bool SamplePath(const FPath& P,double Interval,TArray<FStation>& Out,FString& Code)
{
    Out.Reset();
    if(!FMath::IsFinite(Interval)||Interval<=0||P.Points.Num()<2) {Code=TEXT("INVALID_PATH");return false;}
    int64 Count=1;
    for(int32 I=1;I<P.Points.Num();++I)
    {
        const double N=FMath::CeilToDouble((P.Distances[I]-P.Distances[I-1])/Interval);
        if(N>MaxProfileSamples || (Count+=int64(N))>MaxProfileSamples) {Code=TEXT("SAMPLE_LIMIT_EXCEEDED");return false;}
    }
    Out.Add({P.Points[0],0});
    for(int32 I=1;I<P.Points.Num();++I)
    {
        const double L=P.Distances[I]-P.Distances[I-1];
        const int32 N=FMath::CeilToInt(L/Interval);
        for(int32 J=1;J<=N;++J)
        {
            const double T=double(J)/N;
            Out.Add({FMath::Lerp(P.Points[I-1],P.Points[I],T),FMath::Lerp(P.Distances[I-1],P.Distances[I],T)});
        }
    }
    return true;
}
double PathDistance(const FPath& P,FVector2D V)
{
    double D=TNumericLimits<double>::Max();
    for(int32 I=1;I<P.Points.Num();++I) D=FMath::Min(D,PointSegment(V,P.Points[I-1],P.Points[I]));
    return D;
}
double ProfileAt(TConstArrayView<FStation> S,TConstArrayView<double> H,double Distance)
{
    int32 L=0,R=S.Num()-1;
    while(R-L>1) {const int32 M=(L+R)/2;if(S[M].Distance<=Distance)L=M;else R=M;}
    if(L==R) return H[L];
    return FMath::Lerp(H[L],H[R],FMath::Clamp((Distance-S[L].Distance)/(S[R].Distance-S[L].Distance),0.0,1.0));
}
double JoinedHeight(const FPath& P,TConstArrayView<FStation> S,TConstArrayView<double> H,FVector2D V,double Radius)
{
    double Sum=0,Weights=0,Nearest=TNumericLimits<double>::Max(),Fallback=0;
    for(int32 I=1;I<P.Points.Num();++I)
    {
        const double T=SegmentProjection(V,P.Points[I-1],P.Points[I]);
        const double D=FVector2D::Distance(V,FMath::Lerp(P.Points[I-1],P.Points[I],T));
        const double Z=ProfileAt(S,H,FMath::Lerp(P.Distances[I-1],P.Distances[I],T));
        if(D<Nearest) {Nearest=D;Fallback=Z;}
        const double W=FMath::Square(FMath::Max(0.0,1-FMath::Square(D/Radius)));
        Sum+=W*Z;Weights+=W;
    }
    return Weights>0 ? Sum/Weights:Fallback;
}
double FalloffWeight(double D,double F)
{
    if(D<=0) return 1;
    if(F<=0 || D>=F) return 0;
    const double T=1-D/F;return T*T*(3-2*T);
}
EIntersection TriangleRectangle(FVector2D A,FVector2D B,FVector2D C,FVector2D Min,FVector2D Max,FVector2D Scale)
{
    // cell原点相対・grid正規化で大きなWorld座標の面積消去誤差を避ける。
    const FVector2D Origin=A;
    auto Local=[&](FVector2D P){return FVector2D((P.X-Origin.X)/Scale.X,(P.Y-Origin.Y)/Scale.Y);};
    TArray<FVector2D> Poly={Local(A),Local(B),Local(C)};
    Min=Local(Min);Max=Local(Max);
    for(int32 Axis=0;Axis<2;++Axis) for(int32 Side=0;Side<2;++Side)
    {
        TArray<FVector2D> Next;
        const double Bound=Side==0?Min[Axis]:Max[Axis];
        auto Inside=[&](FVector2D P){return Side==0?P[Axis]>=Bound:P[Axis]<=Bound;};
        for(int32 I=0;I<Poly.Num();++I)
        {
            const FVector2D P=Poly[I],Q=Poly[(I+1)%Poly.Num()];
            const bool IP=Inside(P),IQ=Inside(Q);
            if(IP)Next.Add(P);
            if(IP!=IQ)Next.Add(FMath::Lerp(P,Q,(Bound-P[Axis])/(Q[Axis]-P[Axis])));
        }
        Poly=MoveTemp(Next);
    }
    double Area=0;for(int32 I=0;I<Poly.Num();++I)Area+=Cross(Poly[I],Poly[(I+1)%Poly.Num()]);
    Area=FMath::Abs(Area)*0.5;
    if(Area==0)return EIntersection::None;
    const double Magnitude=FMath::Max(FMath::Abs(Origin.X),FMath::Abs(Origin.Y));
    const double Epsilon=FMath::Max(1.e-12,16*2.2204460492503131e-16*Magnitude/FMath::Min(Scale.X,Scale.Y));
    return Area<=Epsilon?EIntersection::Ambiguous:EIntersection::Positive;
}
EIntersection TrianglePath(FVector2D A,FVector2D B,FVector2D C,const FPath& P,double Radius,FVector2D Scale)
{
    double D=TNumericLimits<double>::Max();
    for(int32 I=1;I<P.Points.Num();++I)
    {
        const FVector2D U=P.Points[I-1],V=P.Points[I];
        if(InTriangle(U,A,B,C)||InTriangle(V,A,B,C)) D=0;
        D=FMath::Min(D,FMath::Min(SegmentDistance(U,V,A,B),FMath::Min(SegmentDistance(U,V,B,C),SegmentDistance(U,V,C,A))));
    }
    if(D==Radius)return EIntersection::None; // 厳密なedge/corner touchはcore保証対象外。
    const double Magnitude=FMath::Max(FMath::Abs(A.X),FMath::Abs(A.Y));
    const double Epsilon=FMath::Max(FMath::Min(Scale.X,Scale.Y)*1.e-10,16*2.2204460492503131e-16*Magnitude);
    if(FMath::Abs(Radius-D)<=Epsilon)return EIntersection::Ambiguous;
    return D>Radius?EIntersection::None:EIntersection::Positive;
}
}
