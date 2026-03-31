// Copyright (c) 2024-2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "TrainingSettings/ScriptSettings.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "LogScholaTraining.h"

inline FString WithQuotes(FString Input)
{
	return FString("\"") + Input + FString("\"");
}

FString FScriptSettings::GetTrainingArgs(FScriptArgBuilder& InArgBuilder) const
{
	//Pass in an ArgBuilder to add additional arguments to the script
	switch (ScriptType)
	{
		case (EScriptType::Python):
			switch (PythonScriptType)
			{
				case (EPythonScript::SB3):
					this->SB3Settings.GenerateTrainingArgs(InArgBuilder);
					break;
				case (EPythonScript::RLLIB):
					this->RLlibSettings.GenerateTrainingArgs(InArgBuilder);
					break;
				default:
					this->CustomPythonScriptSettings.GenerateTrainingArgs(InArgBuilder);
					break;
			}
			break;

		default:
			this->CustomScriptSettings.GenerateTrainingArgs(InArgBuilder);
			break;
	}
	return InArgBuilder.Build();
}

FString FScriptSettings::GetTrainingArgs() const
{
	FScriptArgBuilder TempArgBuilder;
	return GetTrainingArgs(TempArgBuilder);
}

FString FScriptSettings::GetScriptPath() const
{
	switch (ScriptType)
	{
		case (EScriptType::Python):
			switch (PythonScriptType)
			{
				case (EPythonScript::Custom):
					return WithQuotes(CustomPythonScriptSettings.LaunchScript.FilePath);
				default:
					return FString("-m schola.scripts.launch");
			}

		default:
			return WithQuotes(CustomScriptSettings.LaunchScript.FilePath);
	}
}

FLaunchableScript FScriptSettings::GetLaunchableScript() const
{	
	FString ScriptCommand = this->GetScriptPath();
	switch (ScriptType)
	{
		case (EScriptType::Python):
			switch (EnvType)
			{
				case (EPythonEnvironmentType::Conda):
				{
#if PLATFORM_WINDOWS
					// Using explicit .bat as sometimes just conda doesn't get picked up correctly on Windows
					const FString CondaExec = TEXT("conda.bat"); 
#else
					const FString CondaExec = TEXT("conda");
#endif
					const FString Args = FString::Printf(
						TEXT("run --live-stream -n \"%s\" python %s"),
						*this->CondaEnvName,
						*ScriptCommand);
					return FLaunchableScript(CondaExec, Args);
				}
				case (EPythonEnvironmentType::VEnv):
					//Convert to absolute file path
					if (this->CustomPythonPath.FilePath.StartsWith(TEXT("..")))
					{
						return FLaunchableScript(FPaths::ConvertRelativePathToFull(this->CustomPythonPath.FilePath), ScriptCommand);
					}
					else
					{
						return FLaunchableScript(this->CustomPythonPath.FilePath, ScriptCommand);
					}

				case (EPythonEnvironmentType::SystemPath):
					return FLaunchableScript(FString("python"), ScriptCommand);

				default:
					EnsureScholaIsInstalled();
					return FLaunchableScript(GetBuiltInPythonPath(), ScriptCommand);
			}

		default:
			return FLaunchableScript(ScriptCommand);
	}
}

FString FScriptSettings::GetBuiltInPythonPath() const
{
#if PLATFORM_WINDOWS
	return FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/ThirdParty/Python3/Win64/python.exe"));
#elif PLATFORM_MAC
	return FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/ThirdParty/Python3/Mac/bin/python3"));
#elif PLATFORM_LINUX
	return FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/ThirdParty/Python3/Linux/bin/python3"));
#else
	UE_LOG(LogScholaTraining, Warning, TEXT("Unsupported platform, defaulting to Linux Python path"));
	return FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/ThirdParty/Python3/Linux/bin/python3"));
#endif
}

void FScriptSettings::EnsureScholaIsInstalled() const
{
	FString PythonPath = GetBuiltInPythonPath();
	int32 ReturnCode;
	FString StdOut;
	FString StdErr;
	FPlatformProcess::ExecProcess(*PythonPath, TEXT("-m pip show schola"), &ReturnCode, &StdOut, &StdErr);
	// if not installed, install it
	if (ReturnCode != 0)
	{
		FString Command = FString::Printf(TEXT("-m pip install %s[all]"), *(*IPluginManager::Get().FindPlugin(TEXT("Schola"))->GetBaseDir() + FString("/Resources/python")));
		FPlatformProcess::ExecProcess(*PythonPath, *Command, &ReturnCode, &StdOut, &StdErr);
		if (ReturnCode != 0)
		{
			UE_LOG(LogScholaTraining, Error, TEXT("Failed to install Schola python package: %s \n%s"), *StdOut, *StdErr);
		} else{
			UE_LOG(LogScholaTraining, Log, TEXT("Installed Schola python package"));
		}	
	}
}

FScriptSettings::~FScriptSettings()
{
}
