#pragma once
#include "CoreMinimal.h"

namespace LandscapeMCP::Authoring
{
    // UObjectを保持しないread-only snapshot。Rect/Extentの最大座標はinclusive。
    struct FHeightfieldSnapshot
    {
        FVector Origin = FVector::ZeroVector;
        FVector Scale = FVector(100);
        FIntRect Extent;
        FIntRect Rect;
        TArray<uint16> Heights;
        int64 CachedSourceBytes = 0;
        int32 CountX() const { return Rect.Max.X-Rect.Min.X+1; }
        int32 CountY() const { return Rect.Max.Y-Rect.Min.Y+1; }
        double Decode(uint16 H) const { return Origin.Z+(double(H)-32768.0)*Scale.Z/128.0; }
        bool Sample(FVector2D World, TConstArrayView<uint16> Data, double& Height) const;
    };
    // 既存Resolve/Reader/Encodeへの限定bridge。Apply interfaceはPhase 1に存在しない。
    bool ReadSnapshot(const FString& Path, FVector2D WorldMin, FVector2D WorldMax,
        FHeightfieldSnapshot& Out, FString& ErrorCode, FString& Error);
    bool EncodeHeight(double WorldZ, double OriginZ, double ScaleZ, uint16& Out);
}
