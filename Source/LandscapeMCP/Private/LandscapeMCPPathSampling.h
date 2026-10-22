#pragma once
#include "CoreMinimal.h"

namespace LandscapeMCP::Authoring
{
    constexpr int32 MaxPathPoints = 64;
    constexpr int32 MaxProfileSamples = 1024;
    constexpr int32 MaxRequestedVertices = 16384;
    constexpr int32 MaxAabbCandidates = 65536;
    constexpr int32 MaxReadVertices = 16384;
    constexpr int64 MaxCachedSourceBytes = 32ll*1024*1024;
    struct FPath
    {
        TArray<FVector2D> Points;
        TArray<double> Distances;
        double Length = 0;
    };
    struct FStation { FVector2D Position; double Distance = 0; };
    enum class EIntersection { None, Positive, Ambiguous };
    bool ValidatePath(TConstArrayView<FVector2D> Points, double OuterRadius, FPath& Out, FString& Code);
    bool SamplePath(const FPath& Path, double Interval, TArray<FStation>& Out, FString& Code);
    double SegmentProjection(FVector2D P, FVector2D A, FVector2D B);
    double SegmentDistance(FVector2D A, FVector2D B, FVector2D C, FVector2D D);
    double PathDistance(const FPath& Path, FVector2D P);
    double ProfileAt(TConstArrayView<FStation> Stations, TConstArrayView<double> Heights, double S);
    double JoinedHeight(const FPath& Path, TConstArrayView<FStation> Stations, TConstArrayView<double> Heights, FVector2D P, double Radius);
    double FalloffWeight(double OutsideCore, double Falloff);
    EIntersection TriangleRectangle(FVector2D A, FVector2D B, FVector2D C, FVector2D Min, FVector2D Max, FVector2D GridScale);
    EIntersection TrianglePath(FVector2D A, FVector2D B, FVector2D C, const FPath& Path, double Radius, FVector2D GridScale);
}
