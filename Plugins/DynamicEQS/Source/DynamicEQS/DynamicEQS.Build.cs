// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DynamicEQS : ModuleRules
{
	public DynamicEQS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
				// Public headers are automatically picked up via module include paths
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				// Private implementation paths
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"AIModule",
				"NavigationSystem",
				"Schola",
				"StructUtils",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// No Slate needed for runtime plugin
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// add any modules that your module loads dynamically here
			}
		);
	}
}
