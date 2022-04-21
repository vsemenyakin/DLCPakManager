using UnrealBuildTool;

public class DLCPakManager : ModuleRules
{
	public DLCPakManager(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;		
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core"
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
                "ChunkDownloader",
            }
			);


        PrivateIncludePaths.AddRange(
            new string[]
            {
                "ChunkDownloader",
            }
            );
    }
}
