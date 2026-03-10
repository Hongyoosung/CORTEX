// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GameAI_Project : ModuleRules
{
    public GameAI_Project(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "EnhancedInput",
            "AIModule",
            "NavigationSystem",
            "Json",
            "JsonUtilities",
            "GameplayStateTreeModule",
            "StateTreeModule",
            "StructUtils",
            "Schola",
            "NNE",
            "NNERuntimeORT",
            "GameplayTags",
            "Niagara",
            "UMG",
            "Slate",
            "SlateCore",
            "GameplayAbilities",
            "GameplayTags",
            "GameplayTasks"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "GameplayTasks",
        });

        // Add Public directory to include paths
        PublicIncludePaths.Add(ModuleDirectory + "/Public");
    }
}
