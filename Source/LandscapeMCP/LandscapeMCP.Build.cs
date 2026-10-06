using UnrealBuildTool;

public class LandscapeMCP : ModuleRules
{
    public LandscapeMCP(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ToolsetRegistry" });
        PrivateDependencyModuleNames.AddRange(new[] { "Landscape", "UnrealEd", "Foliage", "RenderCore", "Json", "JsonUtilities" });
    }
}
