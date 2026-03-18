// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

public class gRPC : ModuleRules
{
    public gRPC(ReadOnlyTargetRules Target)
        : base(Target)
    {
        Type = ModuleType.External;

        PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, Target.Platform.ToString(), "include"));
        //PublicIncludePaths.Add(Path.Combine(ModuleDirectory, Target.Platform.ToString(), "include"));
        string LibraryPath = Path.Combine(ModuleDirectory, Target.Platform.ToString(), "lib");

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            foreach (FileInfo file in new DirectoryInfo(LibraryPath).GetFiles("*.lib"))
            {
                PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, file.Name));
            }

        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {

            foreach (FileInfo file in new DirectoryInfo(LibraryPath).GetFiles("*.a"))
            {
                PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, file.Name));
            }

        }
        else
        {
            throw new Exception("Unsupported platform " + Target.Platform.ToString());
        }

        PublicDependencyModuleNames.AddRange(new string[] { "OpenSSL", "zlib" });
        PrivateDependencyModuleNames.AddRange(new string[] { });

        PublicDefinitions.Add("GOOGLE_PROTOBUF_NO_RTTI=1");
    }
}
