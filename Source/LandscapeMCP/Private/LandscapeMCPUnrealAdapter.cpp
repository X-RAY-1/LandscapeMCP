#include "LandscapeMCPEngineAdapter.h"
#include "LandscapeMCPApply.h"
#include "LandscapeSettings.h"
#include "Misc/EngineVersion.h"

namespace LandscapeMCP::Engine
{
namespace
{
    // descriptor（LandscapeMCP.uplugin）のVersionNameと一致させる。V05.Adapter.Capabilitiesが照合する。
    constexpr const TCHAR* PluginVersionName = TEXT("0.5.0");
    constexpr const TCHAR* AdapterVersion = TEXT("0.5.0");

    class FUnrealTerrainAdapter final : public ITerrainEngineAdapter
    {
    public:
        virtual FLandscapeMCPCapabilitiesResult Capabilities() const override
        {
            FLandscapeMCPCapabilitiesResult R;
            R.bSuccess = true;
            R.Engine = TEXT("UnrealEngine");
            R.EngineVersion = FEngineVersion::Current().ToString();
            R.Adapter = TEXT("Unreal");
            R.AdapterVersion = AdapterVersion;
            R.ToolsetVersion = GetDefault<ULandscapeMCPToolset>()->GetToolsetVersion();
            R.PluginVersionName = PluginVersionName;
            R.BuildId = FString(ANSI_TO_TCHAR(__DATE__)) + TEXT(" ") + ANSI_TO_TCHAR(__TIME__);
            R.HeightWrite = TEXT("VERIFIED_APPLY");
            R.CollisionVerification = TEXT("CODEC_EXACT");
            R.Undo = TEXT("OWNED_TRANSACTION");
            R.FoliageInspect = TEXT("SUPPORTED");
            R.FoliageApply = TEXT("PRESERVE_IN_PLACE_ONLY");
            const ULandscapeSettings* Settings = GetDefault<ULandscapeSettings>();
            const EFoliageLandscapeSnappingMode Mode = Settings ? Settings->GetFoliageLandscapeSnappingMode() : EFoliageLandscapeSnappingMode::SnapEverywhere;
            R.FoliageSnappingMode = Mode == EFoliageLandscapeSnappingMode::DontSnap ? TEXT("DontSnap")
                : Mode == EFoliageLandscapeSnappingMode::SnapUnderBrushOnly ? TEXT("SnapUnderBrushOnly") : TEXT("SnapEverywhere");
            R.UnsupportedFoliageForms = { TEXT("LANDSCAPE_GRASS"), TEXT("PROCEDURAL_FOLIAGE"), TEXT("NON_STATIC_MESH_FOLIAGE"), TEXT("FOLIAGE_IN_OTHER_LEVEL") };
            R.WorldPartition = TEXT("UNSUPPORTED");
            R.bMutationDisabled = Authoring::MutationDisabled();
            R.Message = TEXT("Read-only capability report. No Landscape, foliage, package or transaction changes.");
            return R;
        }
        virtual bool ReadHeights(const FString& Path, FVector2D Min, FVector2D Max,
            Authoring::FHeightfieldSnapshot& Out, FString& Code, FString& Error) override
        {
            return Authoring::ReadSnapshot(Path, Min, Max, Out, Code, Error);
        }
        virtual bool InspectFoliage(const FString& Path, FVector2D Min, FVector2D Max,
            Foliage::FInventory& Out, FString& Code, FString& Error) override
        {
            return Authoring::CollectFoliage(Path, Min, Max, Out, Code, Error);
        }
    };
}

ITerrainEngineAdapter& GetActiveAdapter()
{
    static FUnrealTerrainAdapter Adapter;
    return Adapter;
}
}
