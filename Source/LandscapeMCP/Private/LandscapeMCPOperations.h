#pragma once
#include "CoreMinimal.h"
#include "LandscapeMCPToolset.h"

namespace LandscapeMCP
{
    enum class EOperation { Sculpt, Smooth, Flatten };
    struct FEditRequest
    {
        FString LandscapePath;
        FVector2D Center = FVector2D::ZeroVector;
        double RadiusCm = 0;
        double Strength = 0;
        double Falloff = 0;
        double TargetHeightCm = 0;
        bool bRaise = true;
        bool bDryRun = true;
        EOperation Operation = EOperation::Sculpt;
    };
    // 操作層はMCPの例外処理に依存せず、明示的な失敗結果を返す。
    FLandscapeMCPResult Create(const FString& LevelPath, const FString& Name, FVector Location, FVector Scale,
        int32 CountX, int32 CountY, int32 Sections, int32 Quads, double InitialHeight, bool bDryRun);
    FLandscapeMCPResult Height(const FString& Path, double X, double Y);
    FLandscapeMCPResult Edit(const FEditRequest& Request);
}
