// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Schola : ModuleRules
{
    public Schola(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(new string[] { });



        PrivateIncludePaths.AddRange(new string[] { "Schola/Private" });

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "InputCore",
            "HeadMountedDisplay",
            "NavigationSystem",
            "AIModule",
            "EnhancedInput",
            "Sockets",
            "Networking",
            "gRPC",
            "DeveloperSettings",
            "NNE","Json", "JsonUtilities",
            "Projects"
        });

        PrivateIncludePathModuleNames.AddRange(new string[] { });
        PrivateDependencyModuleNames.AddRange(new string[] {
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
        });


        DynamicallyLoadedModuleNames.AddRange(new string[] { });
    }
}
