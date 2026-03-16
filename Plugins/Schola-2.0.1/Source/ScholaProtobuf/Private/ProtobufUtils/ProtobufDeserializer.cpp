// Copyright (c) 2023 Advanced Micro Devices, Inc. All Rights Reserved.

#include "ProtobufUtils/ProtobufDeserializer.h"
#include "LogScholaProtobuf.h"

using namespace ProtobufDeserializer;

namespace ProtobufDeserializer
{
	// Deserialize EnvironmentStep -> FEnvStep
	template<>
	void FromProto(const Schola::EnvironmentStep& InEnvStepProto, FEnvStep& OutEnvStep)
	{
		OutEnvStep.Actions.Empty();
		FromProto(InEnvStepProto.updates(), OutEnvStep.Actions);
	}

	template <>
	void FromProto(const Schola::StateUpdate& InStateUpdateProto, FTrainingStateUpdate& OutTrainingStateUpdate)
	{
		OutTrainingStateUpdate.Status = static_cast<EConnectorStatusUpdate>(InStateUpdateProto.status());

		switch (InStateUpdateProto.update_case())
		{
			case (Schola::StateUpdate::kStep):
				OutTrainingStateUpdate.UpdateType = ETrainingUpdateType::STEP;
				FromProto(InStateUpdateProto.step(), OutTrainingStateUpdate.StepData);
				break;
			case (Schola::StateUpdate::kReset):
				OutTrainingStateUpdate.UpdateType = ETrainingUpdateType::RESET;
				FromProto(InStateUpdateProto.reset(), OutTrainingStateUpdate.ResetData);
				break;
			default:
				OutTrainingStateUpdate.UpdateType = ETrainingUpdateType::NONE;
				if (InStateUpdateProto.status() == Schola::GOOD)
				{
					UE_LOG(LogScholaProtobuf, Warning, TEXT("Received StateUpdate with no update case set"));
				} // the else case is that we received closed so there shouldn't be a message body
				break;
		}
	}

	template <>
	void FromProto(const Schola::Reset& InResetProto, FTrainingReset& OutTrainingStateUpdate)
	{
		OutTrainingStateUpdate.Environments.Empty();
		FromProto(InResetProto.environments(), OutTrainingStateUpdate.Environments);
	}

	template <>
	void FromProto(const Schola::Step& InStepProto, FTrainingStep& OutTrainingStateUpdate)
	{
		OutTrainingStateUpdate.EnvSteps.Empty();
		FromProto(InStepProto.environments(), OutTrainingStateUpdate.EnvSteps);
	}

	// Deserialize EnvironmentReset -> FEnvReset
	template<>
	void FromProto(const Schola::EnvironmentSettings& InEnvSettingsProto, FEnvReset& OutEnvReset)
	{
		OutEnvReset.Options.Empty();
        FromProto(InEnvSettingsProto.options(), OutEnvReset.Options);

		OutEnvReset.bHasSeed = false;
		if (InEnvSettingsProto.has_seed())
		{
			OutEnvReset.Seed = InEnvSettingsProto.seed();
			OutEnvReset.bHasSeed = true;
		}
	}

	template <>
	void FromProto(const Schola::Point& InPointProto, TInstancedStruct<FPoint>& OutPoint)
	{
		ProtobufPointDeserializer(OutPoint).Deserialize(InPointProto);
	}

	template <>
	void FromProto(const Schola::Space& InSpaceProto, TInstancedStruct<FSpace>& OutSpace)
	{
		ProtobufSpaceDeserializer(OutSpace).Deserialize(InSpaceProto);
	}

	// Basic Types
	template <>
	void FromProto(const std::string& InString, FString& OutString)
	{
		OutString = FString(InString.c_str());
	}

	template<>
	void FromProto(const Schola::GymConnectorStartRequest& InStartRequestProto, FStartRequest& OutStartRequest)
	{
		switch (InStartRequestProto.autoreset_type())
		{
			case Schola::AutoResetType::NEXT_STEP:
			{
				OutStartRequest.AutoResetType = EAutoResetType::NextStep;
				break;
			}
			case Schola::AutoResetType::SAME_STEP:
			{
				OutStartRequest.AutoResetType = EAutoResetType::SameStep;
				break;
			}
			case Schola::AutoResetType::DISABLED:

			default:
				OutStartRequest.AutoResetType = EAutoResetType::Disabled;
				break;
		}
	}

	// Imitation Learning Deserialization
	template<>
	void FromProto(const Schola::ImitationAgentState& InImitationAgentStateProto, FImitationAgentState& OutImitationAgentState)
	{
		// Deserialize observations
		FromProto(InImitationAgentStateProto.observations(), OutImitationAgentState.Observations);
		
		// Deserialize actions
		FromProto(InImitationAgentStateProto.actions(), OutImitationAgentState.Actions);
		
		// Copy simple fields
		OutImitationAgentState.Reward = InImitationAgentStateProto.reward();
		OutImitationAgentState.bTerminated = InImitationAgentStateProto.terminated();
		OutImitationAgentState.bTruncated = InImitationAgentStateProto.truncated();
		
		// Deserialize info map
		OutImitationAgentState.Info.Empty();
		FromProto(InImitationAgentStateProto.info(), OutImitationAgentState.Info);
	}

	template<>
	void FromProto(const Schola::ImitationEnvironmentState& InImitationEnvStateProto, FImitationEnvironmentState& OutImitationEnvState)
	{
		OutImitationEnvState.AgentStates.Empty();
		FromProto(InImitationEnvStateProto.agent_states(), OutImitationEnvState.AgentStates);
	}

	template<>
	void FromProto(const Schola::ImitationTrainingState& InImitationTrainingStateProto, FImitationTrainingState& OutImitationTrainingState)
	{
		OutImitationTrainingState.EnvironmentStates.Empty();
		FromProto(InImitationTrainingStateProto.environment_states(), OutImitationTrainingState.EnvironmentStates);
	}

	template<>
	void FromProto(const Schola::ImitationState& InImitationStateProto, FImitationState& OutImitationState)
	{
		// Deserialize training state if present
		if (InImitationStateProto.has_training_state())
		{
			FromProto(InImitationStateProto.training_state(), OutImitationState.TrainingState);
		}

		// Deserialize initial state if present
		OutImitationState.bHasInitialState = InImitationStateProto.has_initial_state();
		if (OutImitationState.bHasInitialState)
		{
			// Use existing deserialization for InitialState
			// Need to add support for this
			OutImitationState.InitialState.EnvironmentStates.Empty();
			for (const auto& EnvPair : InImitationStateProto.initial_state().environment_states())
			{
				FInitialEnvironmentState& InitialEnvState = OutImitationState.InitialState.EnvironmentStates.Emplace(EnvPair.first);
				for (const auto& AgentPair : EnvPair.second.agent_states())
				{
					FInitialAgentState& InitialAgentState = InitialEnvState.AgentStates.Add(FString(AgentPair.first.c_str()));
					FromProto(AgentPair.second.observations(), InitialAgentState.Observations);
					FromProto(AgentPair.second.info(), InitialAgentState.Info);
				}
			}
		}
	}
}
