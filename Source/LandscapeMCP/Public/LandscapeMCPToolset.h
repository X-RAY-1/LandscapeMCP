#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "LandscapeMCPToolset.generated.h"

/** 高さと距離の数値はワールド座標のcm。Dry-runではTransactionを開始しない。 */
USTRUCT(BlueprintType)
struct LANDSCAPEMCP_API FLandscapeMCPResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bDryRun = false;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString LandscapePath;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 SampleCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") int32 ChangedSampleCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FVector WorldMin = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") FVector WorldMax = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MinDeltaCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double MaxDeltaCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double HeightCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") double HeightQuantizationCm = 0;
    UPROPERTY(BlueprintReadOnly, Category="Landscape MCP") bool bClipped = false;
};

/** v0.1: Editor限定。World Partition非使用で、表示・ロック解除済みの標準Edit Layerが1つあるLandscapeのみ対応。保存しない。 */
UCLASS()
class LANDSCAPEMCP_API ULandscapeMCPToolset : public UToolsetDefinition
{
    GENERATED_BODY()
public:
    virtual FString GetToolsetVersion() const override { return TEXT("0.1"); }

    /** 明示指定された読み込み済みEditor LevelにLandscapeを作成する。LocationはXY最小隅。
     * Scaleはグリッド座標単位あたりのcm。推奨値は(100,100,100)。InitialWorldHeightは絶対ワールドZのcm。
     * 上限: 16 Component、262144 Sample、各軸1000m。QuadsPerSectionは7,15,31,63。
     * LevelPathは読み込み済みULevelの完全Object Path。Dry-runの既定値はtrue。ディスクへ書き込まない。
     */
    UFUNCTION(meta=(AICallable, NonTransactableToolCall), Category="Landscape MCP")
    static FLandscapeMCPResult CreateLandscape(const FString& LevelPath, const FString& LandscapeName,
        FVector Location, FVector Scale, int32 ComponentCountX, int32 ComponentCountY,
        int32 SectionsPerComponent, int32 QuadsPerSection, double InitialWorldHeight, bool bDryRun = true);

    /** 単一標準Layerの元Heightfieldから、bilinear補間した絶対ワールドZ高度をcmで取得する。
     * LandscapePathは読み込み済みActorの完全Object Path。名前やラベルから推測しない。
     * Texture変更、Layer統合、Transaction作成、保存を行わない。
     */
    UFUNCTION(meta=(AICallable, NonTransactableToolCall), Category="Landscape MCP")
    static FLandscapeMCPResult GetHeight(const FString& LandscapePath, double WorldX, double WorldY);

    /** 円形範囲の高度を加減する。StrengthCmは[0,1000]、bRaiseで符号を選ぶ。RadiusCmは(0,5000]。
     * Falloffは[0,1]で、半径のうち滑らかな縁が占める割合。読み取りは最大16384 Sample。
     * Landscape端で円をclipする。範囲外の中心、穴、非対応対象は拒否する。
     */
    UFUNCTION(meta=(AICallable, NonTransactableToolCall), Category="Landscape MCP")
    static FLandscapeMCPResult SculptRegion(const FString& LandscapePath, FVector2D Center,
        double RadiusCm, double StrengthCm, double Falloff, bool bRaise, bool bDryRun = true);

    /** 3x3近傍平均を同時に1回適用する。Strengthは[0,1]。SculptRegionと同じ制限付き円形ブラシ。 */
    UFUNCTION(meta=(AICallable, NonTransactableToolCall), Category="Landscape MCP")
    static FLandscapeMCPResult SmoothRegion(const FString& LandscapePath, FVector2D Center,
        double RadiusCm, double Strength, double Falloff, bool bDryRun = true);

    /** 絶対高度TargetHeightCmへ近づける。Strengthは[0,1]。同じ制限付き円形ブラシ。 */
    UFUNCTION(meta=(AICallable, NonTransactableToolCall), Category="Landscape MCP")
    static FLandscapeMCPResult FlattenRegion(const FString& LandscapePath, FVector2D Center,
        double RadiusCm, double TargetHeightCm, double Strength, double Falloff, bool bDryRun = true);
};
