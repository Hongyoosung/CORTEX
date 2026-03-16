// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ScholaProtobuf : ModuleRules
{
	public ScholaProtobuf(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicIncludePaths.AddRange(new string[] { });
		PrivateIncludePaths.AddRange(new string[] { "ScholaProtobuf/Private"});
		// Make generated code available to other modules
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "gRPC", "Schola", "ScholaTraining","ScholaImitation", "Projects"});

		PrivateIncludePathModuleNames.AddRange(new string[] {  "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] { });
		DynamicallyLoadedModuleNames.AddRange(new string[] { });
	}
}
