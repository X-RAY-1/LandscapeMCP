#include "Modules/ModuleManager.h"
#include "LandscapeMCPToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

class FLandscapeMCPModule : public IModuleInterface
{
public:
    virtual void StartupModule() override { UToolsetRegistry::RegisterToolsetClass(ULandscapeMCPToolset::StaticClass()); }
    virtual void ShutdownModule() override { UToolsetRegistry::UnregisterToolsetClass(ULandscapeMCPToolset::StaticClass()); }
};
IMPLEMENT_MODULE(FLandscapeMCPModule, LandscapeMCP)
