#pragma once
#include "CoreMinimal.h"
#include "LandscapeMCPToolset.h"
#include "LandscapeMCPHeightfieldAccess.h"
#include "LandscapeMCPFoliageImpact.h"

class UWorld;
class ULevel;

// Engine Adapter境界。MCP facadeとEngine非依存の計算層は、この境界を通してEngineの状態を読む。
// v0.5ではUnrealの実装だけを持つ。書き込み（Apply）はまだ既存のAuthoring::Executeが直接行い、
// この境界へは移していない（docs/v0.5-design.md 2.5）。
namespace LandscapeMCP::Engine
{
    class ITerrainEngineAdapter
    {
    public:
        virtual ~ITerrainEngineAdapter() = default;
        // Engine・Adapter・機能ごとの対応状況。対象のLandscapeは不要。
        virtual FLandscapeMCPCapabilitiesResult Capabilities() const = 0;
        // 対象Landscapeの高さ。Foliageが付いていても読む（何も変更しない）。
        virtual bool ReadHeights(const FString& LandscapePath, FVector2D WorldMin, FVector2D WorldMax,
            Authoring::FHeightfieldSnapshot& Out, FString& ErrorCode, FString& Error) = 0;
        // XY範囲に位置するFoliage instanceと、範囲に関わる判定不能な状態。何も変更しない。
        virtual bool InspectFoliage(const FString& LandscapePath, FVector2D WorldMin, FVector2D WorldMax,
            Foliage::FInventory& Out, FString& ErrorCode, FString& Error) = 0;
    };

    ITerrainEngineAdapter& GetActiveAdapter();
}

namespace LandscapeMCP::Authoring
{
    // Unreal Adapterの実装用。Operations.cppの対象解決（読み取り用途）を使う。
    bool CollectFoliage(const FString& LandscapePath, FVector2D WorldMin, FVector2D WorldMax,
        Foliage::FInventory& Out, FString& ErrorCode, FString& Error);
    // Worldに読み込まれているすべてのLevel（非表示・ロック中を含む）。読み込まれていないstreaming levelはUnknownsへ。
    void LoadedLevels(UWorld* World, TArray<ULevel*>& Levels, TArray<FString>* Unknowns);
}
