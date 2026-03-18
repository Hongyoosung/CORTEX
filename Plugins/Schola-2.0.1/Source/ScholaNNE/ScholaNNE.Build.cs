// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ScholaNNE : ModuleRules
{
	public ScholaNNE(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicIncludePaths.AddRange(new string[] { });
		PrivateIncludePaths.AddRange(new string[] { "ScholaNNE/Private"});
		// Make generated code available to other modules
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "gRPC", "Schola", "Projects", "NNE"});

		PrivateIncludePathModuleNames.AddRange(new string[] {  "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] { });
		DynamicallyLoadedModuleNames.AddRange(new string[] { });
	}
}
