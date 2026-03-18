// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.
using UnrealBuildTool;

public class ScholaImitation : ModuleRules
{
    public ScholaImitation(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "Schola",
            "Projects",
            "ScholaTraining"
        });

        PrivateDependencyModuleNames.AddRange(new string[] { });
    }
}