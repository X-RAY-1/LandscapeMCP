#include "LandscapeMCPCollisionModel.h"
namespace LandscapeMCP::CollisionCodec
{
    bool Build(TConstArrayView<uint16> S,int32 Rows,int32 Cols,FModel& O)
    {
        if(Rows<2||Cols<2||int64(Rows)*Cols!=S.Num())return false;
        O={};O.Rows=Rows;O.Cols=Cols;O.Min=O.Max=double(int32(S[0])-32768);
        for(uint16 H:S){const double V=double(int32(H)-32768);O.Min=FMath::Min(O.Min,V);O.Max=FMath::Max(O.Max,V);}
        O.Range=O.Max-O.Min;O.Step=O.Range/65535.0;O.bEncodedDefined=O.Range!=0;
        O.Encoded.SetNumZeroed(S.Num());
        // Never emulate the engine's undefined NaN-to-integer conversion.
        // Zero here is a canonical geometry representative, not predicted bits.
        if(O.bEncodedDefined)for(int32 I=0;I<S.Num();++I)
            O.Encoded[I]=static_cast<uint16>((double(int32(S[I])-32768)-O.Min)/O.Step);
        return true;
    }
    bool Edit(FModel& O,TConstArrayView<uint16> S,int32 Row,int32 Col,int32 Rows,int32 Cols)
    {
        if(Rows<1||Cols<1||S.Num()!=Rows*Cols||Row<0||Col<0||Row+Rows>O.Rows||Col+Cols>O.Cols)return false;
        double Min=TNumericLimits<double>::Max(),Max=TNumericLimits<double>::Min();
        for(uint16 H:S){const double V=double(int32(H)-32768);Min=FMath::Min(Min,V);Max=FMath::Max(Max,V);}
        const bool Expand=Min<O.Min||Max>O.Max;
        const double NewMin=Expand?FMath::Min(Min,O.Min):O.Min;
        const double NewMax=Expand?FMath::Max(Max,O.Max):O.Max;
        const double Step=(NewMax-NewMin)/65535.0;
        for(int32 Y=0;Y<O.Rows;++Y)for(int32 X=0;X<O.Cols;++X)
        {
            const int32 I=Y*O.Cols+(O.Cols-1-X);
            const bool Touch=Y>=Row&&Y<Row+Rows&&X>=Col&&X<Col+Cols;
            if(!Touch&&!Expand)continue;
            const double V=Touch?double(int32(S[(Y-Row)*Cols+X-Col])-32768):O.Height(I);
            O.Encoded[I]=Step!=0?static_cast<uint16>((V-NewMin)/Step):0;
        }
        O.Min=NewMin;O.Max=NewMax;O.Range=NewMax-NewMin;O.Step=Step;O.bEncodedDefined=Step!=0;
        return true;
    }
    FTriangle Triangle(FVector A,FVector B,FVector C)
    {
        FTriangle T;T.A=A;T.B=B;T.C=C;T.Centroid=(A+B+C)/3;
        T.Normal=FVector::CrossProduct(B-A,C-A).GetSafeNormal();if(T.Normal.Z<0)T.Normal=-T.Normal;
        T.Slope=FMath::RadiansToDegrees(FMath::Atan2(FVector2D(T.Normal.X,T.Normal.Y).Size(),T.Normal.Z));return T;
    }
    FSafety Safety(double S,double C,double Threshold,double Q,double Step,double X,double Y,double N)
    {
        FSafety O;
        // Source rounding error per vertex <= Q/2: each gradient edge <= Q.
        // Codec truncation error in [0,Step]: each gradient edge <= Step.
        const double Metric=FMath::Sqrt(1/(X*X)+1/(Y*Y));
        O.SourceUncertaintyDegrees=FMath::RadiansToDegrees(FMath::Atan(Q*Metric));
        O.CollisionCodecUncertaintyDegrees=FMath::RadiansToDegrees(FMath::Atan(Step*Metric));
        O.NumericDegrees=N;
        O.CombinedSafetyMargin=Threshold-FMath::Max(S,C)-O.SourceUncertaintyDegrees-O.CollisionCodecUncertaintyDegrees-N;
        O.bSafe=O.CombinedSafetyMargin>0;
        O.bNearLimit=!O.bSafe&&S<=Threshold&&C<=Threshold;return O;
    }
}
