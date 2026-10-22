#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "LandscapeMCPCollisionModel.h"
#include "Chaos/HeightField.h"

namespace LandscapeMCP::CollisionCodecTests
{
    using namespace CollisionCodec;
    bool Oracle(FAutomationTestBase& T,TConstArrayView<uint16> S,int32 Rows,int32 Cols)
    {
        FModel M;if(!T.TestTrue(TEXT("build"),Build(S,Rows,Cols,M)))return false;
        Chaos::FHeightField H(S,TConstArrayView<uint8>(),Rows,Cols,Chaos::FVec3(1));
        T.TestEqual(TEXT("min"),M.Min,H.GeomData.MinValue);T.TestEqual(TEXT("max"),M.Max,H.GeomData.MaxValue);
        T.TestEqual(TEXT("step"),M.Step,H.GeomData.HeightPerUnit);
        for(int32 I=0;I<S.Num();++I)
        {
            if(M.bEncodedDefined)T.TestEqual(TEXT("encoded exact"),M.Encoded[I],H.GeomData.Heights[I]);
            T.TestEqual(TEXT("decoded exact"),M.Height(I),H.GetHeight(I));
        }
        if(!M.bEncodedDefined)T.AddInfo(FString::Printf(TEXT("ZERO_RANGE encodedUndefined=1 actualFirstEncoded=%u decoded=%.12f"),H.GeomData.Heights[0],H.GetHeight(0)));
        return true;
    }
    bool Run(FAutomationTestBase& T,const FString& Case)
    {
        if(Case==TEXT("EncodeDecode"))
        {
            TArray<uint16> S={0,1,32767,32768,65534,65535};Oracle(T,S,2,3);
            FRandomStream R(50865535);for(int32 N=0;N<100;++N){S.Reset();for(int32 I=0;I<64;++I)S.Add(uint16(R.RandRange(0,65535)));Oracle(T,S,8,8);}
            S={32768,32769,32768,32769};return Oracle(T,S,2,2);
        }
        if(Case==TEXT("Flat")){TArray<uint16> S;S.Init(32768,64);return Oracle(T,S,8,8);}
        if(Case==TEXT("NegativeHeight")){TArray<uint16> S={0,10,100,32767};return Oracle(T,S,2,2);}
        if(Case==TEXT("RangeExpansion")||Case==TEXT("RangeContraction"))
        {
            TArray<uint16> S={32768,32781,32795,32868,32770,32799,32821,32850,32780};
            FModel M;Build(S,3,3,M);Chaos::FHeightField H(S,TConstArrayView<uint8>(),3,3,Chaos::FVec3(1));
            const double Before=M.Height(1);TArray<uint16> Patch={33000};
            Edit(M,Patch,1,1,1,1);H.EditHeights(Patch,1,1,1,1);
            for(int32 I=0;I<9;++I){T.TestEqual(TEXT("incremental encoded"),M.Encoded[I],H.GeomData.Heights[I]);T.TestEqual(TEXT("incremental decode"),M.Height(I),H.GetHeight(I));}
            T.TestTrue(TEXT("unchanged source requantized"),Before!=M.Height(1));
            T.AddInfo(FString::Printf(TEXT("RANGE_EXPANSION unchangedRaw=%u before=%.12f after=%.12f min=%.12f max=%.12f step=%.12f"),S[1],Before,M.Height(1),M.Min,M.Max,M.Step));
            Patch[0]=32768;Edit(M,Patch,1,1,1,1);H.EditHeights(Patch,1,1,1,1);
            T.TestEqual(TEXT("incremental no contraction"),M.Max,232.0);
            for(int32 I=0;I<9;++I)T.TestEqual(TEXT("contraction attempt encoded"),M.Encoded[I],H.GeomData.Heights[I]);
            S[4]=32768;FModel Rebuild;Build(S,3,3,Rebuild);T.TestEqual(TEXT("full rebuild contracts"),Rebuild.Max,100.0);Oracle(T,S,3,3);
            T.AddInfo(TEXT("RANGE_CONTRACTION EditHeights=NOT_APPLICABLE; fresh BuildGeomData contracts to current source range."));return true;
        }
        if(Case==TEXT("NativeTriangles")||Case==TEXT("Normal")||Case==TEXT("Slope"))
        {
            TArray<uint16> S={32768,32800,32780,32820};FModel M;Build(S,2,2,M);
            const auto P=[&](int32 X,int32 Y,int32 I){return FVector(X*100,Y*150,M.Height(I)*100/128);};
            Chaos::FHeightField H(S,TConstArrayView<uint8>(),2,2,Chaos::FVec3(100,150,100.0/128));
            H.BoundingBox(); // GridCast consumes CachedBounds; production shape setup populates it.
            for(bool Upper:{false,true})
            {
                const auto E=Triangle(P(0,0,0),Upper?P(0,1,2):P(1,0,1),P(1,1,3));
                double Time;Chaos::FVec3 Point,Normal;int32 Face;
                const bool Hit=H.Raycast(E.Centroid+FVector(0,0,2000),FVector(0,0,-1),4000,0,Time,Point,Normal,Face);
                T.TestTrue(TEXT("native hit"),Hit);T.TestEqual(TEXT("triangle identity"),Face,Upper?1:0);
                T.TestTrue(TEXT("normal"),FVector::Dist(Normal,E.Normal)<1.e-12);
                T.TestEqual(TEXT("centroid"),Point.Z,E.Centroid.Z,1.e-10);
                T.TestEqual(TEXT("slope"),FMath::RadiansToDegrees(FMath::Acos(Normal.Z)),E.Slope,1.e-10);
            }
            return true;
        }
        // Explicit threshold crossing: the codec increases this edge gradient.
        // Exhaustively search source levels 1..100 within range 0..2016.
        FModel M;TArray<uint16> S;S.Init(32768,9);S[2]=34784;
        bool Found=false;double Source=0,Predicted=0;FTriangle Expected;
        for(int32 I=1;I<=100&&!Found;++I)
        {
            S[0]=S[3]=uint16(32768+I);S[1]=S[4]=uint16(32768+I+32);Build(S,3,3,M);
            Source=FMath::RadiansToDegrees(FMath::Atan(25.0/100));
            Expected=Triangle(FVector(0,0,M.Height(0)*100/128),FVector(100,0,M.Height(1)*100/128),FVector(100,100,M.Height(4)*100/128));
            Predicted=Expected.Slope;Found=Predicted>Source;
        }
        Chaos::FHeightField H(S,TConstArrayView<uint8>(),3,3,Chaos::FVec3(100,100,100.0/128));H.BoundingBox();
        double Time;Chaos::FVec3 Point,Normal;int32 Face;
        const bool Hit=H.Raycast(Expected.Centroid+FVector(0,0,2000),FVector(0,0,-1),4000,0,Time,Point,Normal,Face);
        T.TestTrue(TEXT("threshold actual hit"),Hit);if(!Hit)return false;
        T.TestTrue(TEXT("threshold predicted actual normal"),FVector::Dist(Normal,Expected.Normal)<1.e-12);
        T.TestEqual(TEXT("threshold actual slope"),FMath::RadiansToDegrees(FMath::Acos(Normal.Z)),Predicted,1.e-10);
        T.TestTrue(TEXT("crossing fixture found"),Found);
        const double Crossing=(Source+Predicted)/2;
        const double Threshold=Case==TEXT("ThresholdAbove")?Source-0.01:Case==TEXT("ThresholdCrossing")?Crossing:Case==TEXT("NearLimit")?FMath::Max(Source,Predicted)+0.001:Case==TEXT("ThresholdBelow")?FMath::Max(Source,Predicted)+0.01:30;
        const auto Guard=Safety(Source,Predicted,Threshold,100.0/128,M.Step*100/128,100,100,1.e-10);
        T.AddInfo(FString::Printf(TEXT("CODEC_THRESHOLD case=%s source=%.12f predicted=%.12f actual=%.12f threshold=%.12f sourceUncertainty=%.12f codecUncertainty=%.12f numeric=%.12g margin=%.12f safe=%d near=%d"),*Case,Source,Predicted,FMath::RadiansToDegrees(FMath::Acos(Normal.Z)),Threshold,Guard.SourceUncertaintyDegrees,Guard.CollisionCodecUncertaintyDegrees,Guard.NumericDegrees,Guard.CombinedSafetyMargin,Guard.bSafe,Guard.bNearLimit));
        if(Case==TEXT("BothSafe"))T.TestTrue(TEXT("both safe with conservative margin"),Guard.bSafe);
        else T.TestFalse(TEXT("unsafe/near never success"),Guard.bSafe);
        if(Case==TEXT("ThresholdCrossing")){T.TestTrue(TEXT("source below"),Source<Threshold);T.TestTrue(TEXT("codec above"),Predicted>Threshold);}
        if(Case==TEXT("ThresholdBelow")){T.TestTrue(TEXT("geometrically below"),Source<Threshold&&Predicted<Threshold);T.TestTrue(TEXT("below but uncertainty near limit"),Guard.bNearLimit);}
        if(Case==TEXT("NearLimit"))T.TestTrue(TEXT("near limit classified"),Guard.bNearLimit);
        return true;
    }
}
#define CODEC_TEST(Class,Name) \
IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class,"LandscapeMCP.V04.CollisionCodec." Name,EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter) \
bool Class::RunTest(const FString&){return LandscapeMCP::CollisionCodecTests::Run(*this,TEXT(Name));}
CODEC_TEST(FV04CodecEncode,"EncodeDecode")
CODEC_TEST(FV04CodecFlat,"Flat")
CODEC_TEST(FV04CodecNegative,"NegativeHeight")
CODEC_TEST(FV04CodecExpand,"RangeExpansion")
CODEC_TEST(FV04CodecContract,"RangeContraction")
CODEC_TEST(FV04CodecTriangles,"NativeTriangles")
CODEC_TEST(FV04CodecNormal,"Normal")
CODEC_TEST(FV04CodecSlope,"Slope")
CODEC_TEST(FV04CodecBelow,"ThresholdBelow")
CODEC_TEST(FV04CodecAbove,"ThresholdAbove")
CODEC_TEST(FV04CodecCrossing,"ThresholdCrossing")
CODEC_TEST(FV04CodecNear,"NearLimit")
CODEC_TEST(FV04CodecBothSafe,"BothSafe")
#undef CODEC_TEST
#endif
