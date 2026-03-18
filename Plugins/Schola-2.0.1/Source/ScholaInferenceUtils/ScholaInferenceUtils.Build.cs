// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.
using UnrealBuildTool;

public class ScholaInferenceUtils : ModuleRules
{
    public ScholaInferenceUtils(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "Schola"
        });

        PrivateDependencyModuleNames.AddRange(new string[] { });
    }
}