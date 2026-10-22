#pragma once
#include "CoreMinimal.h"

// UE 5.8 Chaos HeightField.cpp BuildGeomData/EditGeomData, uint16 input.
// No UObject, physics query, or public schema dependency.
namespace LandscapeMCP::CollisionCodec
{
    struct FModel
    {
        int32 Rows=0,Cols=0;
        double Min=0,Max=0,Range=0,Step=0;
        TArray<uint16> Encoded;
        // The engine casts 0/0 to uint16 for constant fields. C++ does not define
        // that conversion. Geometry is nevertheless Min + any uint16 * 0 = Min.
        bool bEncodedDefined=true;
        double Height(int32 I) const {return Min+double(Encoded[I])*Step;}
    };
    bool Build(TConstArrayView<uint16> Source,int32 Rows,int32 Cols,FModel& Out);
    // Input patch columns follow EditHeights (inverted relative to stored data).
    bool Edit(FModel& Model,TConstArrayView<uint16> Patch,int32 Row,int32 Col,int32 Rows,int32 Cols);
    struct FTriangle
    {
        FVector A,B,C,Centroid,Normal;
        double Slope=0;
    };
    FTriangle Triangle(FVector A,FVector B,FVector C);
    struct FSafety
    {
        double SourceUncertaintyDegrees=0,CollisionCodecUncertaintyDegrees=0;
        double NumericDegrees=0,CombinedSafetyMargin=0;
        bool bSafe=false,bNearLimit=false;
    };
    FSafety Safety(double SourceSlope,double CollisionSlope,double Threshold,
        double SourceQuantum,double CodecStep,double ScaleX,double ScaleY,double NumericDegrees);
}
