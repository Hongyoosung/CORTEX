// Copyright (c) 2023 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once

#include "Points/Point.h"
#include "Points/MultiBinaryPoint.h"
#include "Points/BoxPoint.h"
#include "Points/DiscretePoint.h"
#include "Points/DictPoint.h"
#include "TrainingDataTypes/TrainingUpdate.h"
#include "TrainingDataTypes/EnvironmentUpdate.h"
#include "TrainingDataTypes/StartRequest.h"
#include "Spaces/Space.h"
#include "Spaces/DictSpace.h"
#include "Spaces/MultiBinarySpace.h"
#include "Spaces/DiscreteSpace.h"
#include "Spaces/MultiDiscreteSpace.h"
#include "Spaces/BoxSpace.h"
#include "Spaces/BoxSpaceDimension.h"
#include "StructUtils/InstancedStruct.h"
#include "ImitationConnectors/AbstractImitationConnector.h"
THIRD_PARTY_INCLUDES_START
#include "GymConnector.pb.h"
#include "Spaces.pb.h"
#include "ImitationState.pb.h"
THIRD_PARTY_INCLUDES_END

/**
 * @brief A class that deserializes protobuf Point messages into Unreal Point structures.
 * @details This class converts protobuf Point messages received from network or storage
 * back into their corresponding Unreal Engine representations (Box, Discrete, MultiBinary,
 * MultiDiscrete, Dict) for use in the Schola framework.
 */
class SCHOLAPROTOBUF_API ProtobufPointDeserializer
{
	TInstancedStruct<FPoint>& DeserializedPoint;

public:
	/**
	 * @brief Constructs a deserializer with a target Unreal Point buffer.
	 * @param[in,out] InitialPoint Reference to the TInstancedStruct<FPoint> to fill during deserialization.
	 */
	ProtobufPointDeserializer(TInstancedStruct<FPoint>& InitialPoint)
		: DeserializedPoint(InitialPoint){};

	void Deserialize(const Schola::Point& InPoint)
	{
		switch (InPoint.point_case())
		{
			case (Schola::Point::kMultiBinaryPoint):
				this->Deserialize(InPoint.multi_binary_point());
				break;

			case (Schola::Point::kBoxPoint):
				this->Deserialize(InPoint.box_point());
				break;

			case (Schola::Point::kDictPoint):
				this->Deserialize(InPoint.dict_point());
				break;

			case (Schola::Point::kDiscretePoint):
				this->Deserialize(InPoint.discrete_point());
				break;

			case (Schola::Point::kMultiDiscretePoint):
				this->Deserialize(InPoint.multi_discrete_point());
				break;
		}
	};

	void Deserialize(const Schola::DictPoint& InDictPoint) 
	{
		DeserializedPoint.InitializeAs<FDictPoint>();
		FDictPoint& Point = DeserializedPoint.GetMutable<FDictPoint>();
		for (auto& Pair : InDictPoint.values())
		{
			TInstancedStruct<FPoint>& SubPoint = Point.Points.Add(FString(Pair.first.c_str()));
			ProtobufPointDeserializer(SubPoint).Deserialize(Pair.second);
		}
	};

	void Deserialize(const Schola::MultiBinaryPoint& InBinaryPoint) 
	{
		DeserializedPoint.InitializeAs<FMultiBinaryPoint>(InBinaryPoint.values().data(), InBinaryPoint.values_size());
	};

	void Deserialize(const Schola::BoxPoint& InBoxPoint) 
	{
		DeserializedPoint.InitializeAs<FBoxPoint>(InBoxPoint.values().data(), InBoxPoint.values_size());
	};

	void Deserialize(const Schola::MultiDiscretePoint& InMultiDiscretePoint)
	{
		DeserializedPoint.InitializeAs<FMultiDiscretePoint>(InMultiDiscretePoint.values().data(), InMultiDiscretePoint.values_size());
	};

	void Deserialize(const Schola::DiscretePoint& InDiscretePoint)
	{
		DeserializedPoint.InitializeAs<FDiscretePoint>(InDiscretePoint.value());
	};

};

/**
 * @brief A class that deserializes protobuf Space messages into Unreal Space structures.
 * @details This class converts protobuf Space messages (which define observation and action spaces)
 * back into their corresponding Unreal Engine representations (Box, Discrete, MultiBinary,
 * MultiDiscrete, Dict) for use in the Schola framework.
 */
class SCHOLAPROTOBUF_API ProtobufSpaceDeserializer
{
	TInstancedStruct<FSpace>& DeserializedSpace;

public:
	/**
	 * @brief Constructs a deserializer with a target Unreal Space buffer.
	 * @param[in,out] InitialSpace Reference to the TInstancedStruct<FSpace> to fill during deserialization.
	 */
	ProtobufSpaceDeserializer(TInstancedStruct<FSpace>& InitialSpace)
		: DeserializedSpace(InitialSpace) {}

	void Deserialize(const Schola::Space& InSpace)
	{
		switch (InSpace.space_case())
		{
			case Schola::Space::kDictSpace:
				this->Deserialize(InSpace.dict_space());
				break;
			case Schola::Space::kMultiBinarySpace:
				this->Deserialize(InSpace.multi_binary_space());
				break;
			case Schola::Space::kMultiDiscreteSpace:
				this->Deserialize(InSpace.multi_discrete_space());
				break;
			case Schola::Space::kDiscreteSpace:
				this->Deserialize(InSpace.discrete_space());
				break;
			case Schola::Space::kBoxSpace:
				this->Deserialize(InSpace.box_space());
				break;
			case Schola::Space::SPACE_NOT_SET:
			default:
				break;
		}
	};

	void Deserialize(const Schola::DictSpace& InDictSpace)
	{
		DeserializedSpace.InitializeAs<FDictSpace>();
		FDictSpace& Space = DeserializedSpace.GetMutable<FDictSpace>();
		for (auto& Pair : InDictSpace.spaces())
		{
			TInstancedStruct<FSpace>& SubSpace = Space.Spaces.Add(FString(Pair.first.c_str()));
			ProtobufSpaceDeserializer(SubSpace).Deserialize(Pair.second);
		}
	};

	void Deserialize(const Schola::MultiBinarySpace& InMultiBinarySpace)
	{
		DeserializedSpace.InitializeAs<FMultiBinarySpace>(InMultiBinarySpace.shape());
	};

	void Deserialize(const Schola::DiscreteSpace& InDiscreteSpace)
	{
		DeserializedSpace.InitializeAs<FDiscreteSpace>(InDiscreteSpace.high());
	};

	void Deserialize(const Schola::MultiDiscreteSpace& InMultiDiscreteSpace)
	{
		DeserializedSpace.InitializeAs<FMultiDiscreteSpace>(InMultiDiscreteSpace.high().data(), InMultiDiscreteSpace.high_size());
	};

	void Deserialize(const Schola::BoxSpace& InBoxSpace)
	{
		DeserializedSpace.InitializeAs<FBoxSpace>();
		FBoxSpace& Space = DeserializedSpace.GetMutable<FBoxSpace>();
		// Dimensions
		Space.Dimensions.Empty();
		Space.Dimensions.Reserve(InBoxSpace.dimensions_size());
		for (const auto& Dim : InBoxSpace.dimensions())
		{
			Space.Dimensions.Add(FBoxSpaceDimension(Dim.low(), Dim.high()));
		}
		// Shape
		Space.Shape.Empty();
		Space.Shape.Append(InBoxSpace.shape_dimensions().data(), InBoxSpace.shape_dimensions_size());
	};
};


/**
 * @brief A namespace containing functions to deserialize protobuf messages into Unreal Engine types
 */
namespace ProtobufDeserializer
{

	template<typename UnrealV, typename ProtoV>
	inline void FromProto(const ProtoV& InProtobufValue, UnrealV& OutUnrealValue)
	{
		OutUnrealValue = InProtobufValue;
	}

	template <typename ProtoV>
	void FromProto(const google::protobuf::RepeatedField<ProtoV>& InProtobufRepeatedField, TArray<ProtoV>& OutUnrealArray)
	{
		OutUnrealArray.Empty();
		OutUnrealArray.Reserve(InProtobufRepeatedField.size());
		for (int i = 0; i < InProtobufRepeatedField.size(); ++i)
		{
			OutUnrealArray.Add(InProtobufRepeatedField.Get(i));
		}
	}

	template <typename UnrealV, typename ProtoV>
	void FromProto(const google::protobuf::RepeatedPtrField<ProtoV>& InProtobufRepeatedField, TArray<UnrealV>& OutUnrealArray)
	{
		OutUnrealArray.Empty();
		OutUnrealArray.Reserve(InProtobufRepeatedField.size());
		for (int i = 0; i < InProtobufRepeatedField.size(); ++i)
		{
			UnrealV Value;
			FromProto<UnrealV, ProtoV>(InProtobufRepeatedField.Get(i), Value);
			OutUnrealArray.Add(MoveTemp(Value));
		}
	}

	template <typename UnrealV, typename ProtoV, typename KeyType>
	void FromProto(const google::protobuf::Map<KeyType, ProtoV>& InProtobufMap, TMap<KeyType, UnrealV>& OutUnrealMap)
	{
		OutUnrealMap.Empty();
		for (const auto& Pair : InProtobufMap)
		{
			UnrealV Value;
			FromProto<UnrealV, ProtoV>(Pair.second, Value);
			OutUnrealMap.Add(Pair.first, MoveTemp(Value));
		}
	}

	template <typename UnrealV, typename ProtoV>
	void FromProto(const google::protobuf::Map<std::string, ProtoV>& InProtobufMap, TMap<FString, UnrealV>& OutUnrealMap)
	{
		OutUnrealMap.Empty();
		for (const auto& Pair : InProtobufMap)
		{
			UnrealV Value;
			FromProto<UnrealV, ProtoV>(Pair.second, Value);
			OutUnrealMap.Add(FString(Pair.first.c_str()), MoveTemp(Value));
		}
	}

	// Update Messages
	template<>
	SCHOLAPROTOBUF_API void FromProto(const Schola::StateUpdate& InStateUpdate, FTrainingStateUpdate& OutTrainingStateUpdate);

	template<>
	SCHOLAPROTOBUF_API void FromProto(const Schola::Reset& InResetProto, FTrainingReset& OutTrainingStateUpdate);

	template<>
	SCHOLAPROTOBUF_API void FromProto(const Schola::Step& InStepProto, FTrainingStep& OutTrainingStateUpdate);

	template<>
	SCHOLAPROTOBUF_API void FromProto(const Schola::EnvironmentSettings& InEnvSettingsProto, FEnvReset& OutEnvResetSettings);

	template<>
	SCHOLAPROTOBUF_API void FromProto(const Schola::EnvironmentStep& InEnvStepProto, FEnvStep& OutEnvStep);

	template<>
	SCHOLAPROTOBUF_API void FromProto(const Schola::GymConnectorStartRequest& InStartRequestProto, FStartRequest& OutStartRequest);

	// Spaces and Points

	template<>
	SCHOLAPROTOBUF_API void FromProto(const Schola::Point& InPointProto, TInstancedStruct<FPoint>& OutPoint);

	template<>
	SCHOLAPROTOBUF_API void FromProto(const Schola::Space& InSpaceProto, TInstancedStruct<FSpace>& OutSpace);

	// Basic Types
	template<>
	SCHOLAPROTOBUF_API void FromProto(const std::string& InString, FString& OutString);

	// Imitation Learning Messages
	template<>
	SCHOLAPROTOBUF_API void FromProto(const Schola::ImitationAgentState& InImitationAgentStateProto, FImitationAgentState& OutImitationAgentState);

	template<>
	SCHOLAPROTOBUF_API void FromProto(const Schola::ImitationEnvironmentState& InImitationEnvStateProto, FImitationEnvironmentState& OutImitationEnvState);

	template<>
	SCHOLAPROTOBUF_API void FromProto(const Schola::ImitationTrainingState& InImitationTrainingStateProto, FImitationTrainingState& OutImitationTrainingState);

	template<>
	SCHOLAPROTOBUF_API void FromProto(const Schola::ImitationState& InImitationStateProto, FImitationState& OutImitationState);
};


