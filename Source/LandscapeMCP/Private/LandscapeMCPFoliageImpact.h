#pragma once
#include "CoreMinimal.h"
#include "LandscapeMCPHeightfieldAccess.h"
#include "LandscapeMCPTerrainOperationResult.h"

// Engine非依存のFoliage影響評価。UObjectを保持せず、Engine Adapterが集めたsampleだけを使う。
namespace LandscapeMCP::Foliage
{
    enum class EBase : uint8 { TargetLandscape, OtherComponent, Unattached };
    const TCHAR* ToString(EBase Base);

    // Foliage instance 1つ分。位置・回転・Scaleは比較用にそのまま保持する。
    struct FSample
    {
        FString FoliageType;
        FString Mesh;
        FVector Location = FVector::ZeroVector;
        FRotator Rotation = FRotator::ZeroRotator;
        FVector Scale = FVector::OneVector;
        double ZOffsetCm = 0;
        uint32 Flags = 0;
        EBase Base = EBase::Unattached;
        // 判定できない形式（Actor Foliage、Procedural、他Levelなど）。空なら判定可能。
        FString UnknownReason;
        bool operator==(const FSample& B) const;
    };

    struct FInventory
    {
        TArray<FSample> Samples;
        // 範囲全体に関わる判定不能な状態（Grass、Procedural Foliage Volumeなど）。
        TArray<FString> Unknowns;
        int32 ScannedCount = 0;
        bool bScanLimitExceeded = false;
        // 対象Landscapeのどれかのcollision componentに付属instanceがあるか（範囲外を含む）。
        bool bTargetHasAttachedFoliage = false;
        FString SnappingMode;
        bool bSnappingSafe = false;
    };

    // 走査するinstanceの上限。超えたら判定不能として扱う。
    constexpr int32 MaxScannedInstances = 65536;
    constexpr int32 MaxReportedSamples = 64;
    // 局所傾斜の変化を「変化あり」とみなす閾値（度）。
    constexpr double SlopeChangeThresholdDegrees = 0.01;

    // Planned（Snapshot.Heightsと同じ並び）を書き込んだ場合の影響を、Foliageを動かさない前提で予測する。
    // Plannedが空ならSnapshotの地形そのもの（変化なし）として扱う。
    FLandscapeMCPFoliageImpact Evaluate(const Authoring::FHeightfieldSnapshot& Snapshot, TConstArrayView<uint16> Planned, const FInventory& Inventory);

    // 地形の定義（Foliage gate・InspectFoliage・Applyの事後検証で共通）。
    // Landscapeの実三角形（描画とChaos collisionと同じ、セルの対角線00-11で分割）の重心座標補間で高度を求める。
    // 汎用のFHeightfieldSnapshot::Sample（bilinear）は使わない。bilinearは非平面セルの中で実三角形と違う値になる。
    //  - セルの選択: 局所座標のfloor。Snapshotの最大辺上の点は最後のセル（f=1）に属する。
    //  - 三角形の選択（主三角形）: fx >= fy なら Lower(00,10,11)、そうでなければ Upper(00,01,11)。対角線上はLower。
    //    高度は辺・対角線・頂点の上で連続なので、どちらの三角形でも同じ値になる。
    //  - Snapshotの外（端は含む）・非有限の座標: false（判定不能）。
    bool TerrainAt(const Authoring::FHeightfieldSnapshot& Snapshot, TConstArrayView<uint16> Heights, FVector2D World, double& HeightCm, double& SlopeDegrees);

    // Before→Afterの変化。高度は主三角形、傾斜の変化はその点を含むすべての三角形（辺上は2つ、頂点上は最大6つ）の最大値。
    // Snapshot外の三角形は、Snapshot内にある頂点がBefore==Afterなら変化なし（書き込みはSnapshotの矩形の中だけ）、
    // そうでなければ評価できないとしてbCompleteをfalseにする。Landscapeの外には三角形がないので数えない。
    struct FTerrainChange
    {
        double BeforeCm = 0, AfterCm = 0, DeltaCm = 0;
        double SlopeChangeDegrees = 0;
        bool bComplete = false;
    };
    bool ChangeAt(const Authoring::FHeightfieldSnapshot& Snapshot, TConstArrayView<uint16> Before, TConstArrayView<uint16> After, FVector2D World, FTerrainChange& Out);

    // 分類の閾値判定（R3の固定値）。高さは |Δ| > HalfStepCm、傾斜は変化 > SlopeChangeThresholdDegrees（いずれも等号は変化なし）。
    enum class EClass : uint8 { Unchanged, Floating, Buried, SlopeChanged, MayBeMoved };
    EClass Classify(double DeltaCm, double SlopeChangeDegrees, double HalfStepCm, bool bSnapRisk);

    // 2つのinventoryのinstanceが完全に一致するか（順序を含む）。範囲の判定不能・走査上限・付属の有無も比べる。
    bool SameInstances(const FInventory& A, const FInventory& B);
    // instanceに加えて、安全性の前提（snap mode、bSnappingSafe、走査件数）も同じか。書き込み直前の再確認に使う。
    bool SameSafetyState(const FInventory& A, const FInventory& B);
}
