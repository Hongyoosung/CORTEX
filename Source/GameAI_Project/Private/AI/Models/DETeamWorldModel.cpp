// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/Models/DETeamWorldModel.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"
#include "NNERuntime.h"

UDETeamWorldModel::UDETeamWorldModel()
	: ModelInstance(nullptr)
	, InputTensorName(TEXT("input"))
	, OutputTensorName(TEXT("output"))
	, bIsModelLoaded(false)
	, AverageLatencyMs(0.0f)
{
}

bool UDETeamWorldModel::InitModel(const FString& ModelPath)
{
	using namespace UE::NNE;

	bIsModelLoaded = false;
	AverageLatencyMs = 0.0f;

	// 1. 모델 파일 존재 여부 확인
	if (!FPaths::FileExists(ModelPath))
	{
		UE_LOG(LogTemp, Error,
			TEXT("UDETeamWorldModel: Model file not found at %s"), *ModelPath);
		return false;
	}

	// 2. 파일을 TArray<uint8>로 로드
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *ModelPath))
	{
		UE_LOG(LogTemp, Error,
			TEXT("UDETeamWorldModel: Failed to load model file: %s"), *ModelPath);
		return false;
	}

	UE_LOG(LogTemp, Log,
		TEXT("UDETeamWorldModel: Loaded model data (%d bytes) from %s"),
		FileData.Num(), *ModelPath);

	// 3. UNNEModelData 객체 생성 + Init
	UNNEModelData* ModelData = NewObject<UNNEModelData>(GetTransientPackage());
	if (!ModelData)
	{
		UE_LOG(LogTemp, Error,
			TEXT("UDETeamWorldModel: Failed to create UNNEModelData object"));
		return false;
	}

	const FString FileExt = FPaths::GetExtension(ModelPath); // ex) "onnx"
	ModelData->Init(
		FileExt,
		TConstArrayView64<uint8>(FileData),
		TMap<FString, TConstArrayView64<uint8>>()  // 추가 버퍼 없음
	);

	// 4. NNE Runtime 얻기 (ONNX Runtime CPU 백엔드)
	TWeakInterfacePtr<INNERuntimeCPU> Runtime =
		UE::NNE::GetRuntime<INNERuntimeCPU>(TEXT("NNERuntimeORTCpu"));

	if (!Runtime.IsValid())
	{
		UE_LOG(LogTemp, Error,
			TEXT("UDETeamWorldModel: Failed to get NNE runtime 'NNERuntimeORTCpu'"));
		// 모델 없이 스텁 모드로라도 게임을 돌리고 싶다면 true 반환
		return true;
	}

	// 5. 모델 생성
	TSharedPtr<IModelCPU> Model = Runtime->CreateModelCPU(ModelData);
	if (!Model.IsValid())
	{
		UE_LOG(LogTemp, Error,
			TEXT("UDETeamWorldModel: Failed to create NNE model from data"));
		return true; // 스텁 모드 허용
	}

	// 6. 모델 인스턴스 생성
	ModelInstance = Model->CreateModelInstanceCPU();
	if (!ModelInstance.IsValid())
	{
		UE_LOG(LogTemp, Error,
			TEXT("UDETeamWorldModel: Failed to create model instance"));
		return true;
	}

	// 7. 입력/출력 텐서 정보 검사
	TConstArrayView<FTensorDesc> InputDescs = ModelInstance->GetInputTensorDescs();
	TConstArrayView<FTensorDesc> OutputDescs = ModelInstance->GetOutputTensorDescs();

	if (InputDescs.Num() == 0 || OutputDescs.Num() == 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("UDETeamWorldModel: Invalid tensor descriptors"));
		ModelInstance.Reset();
		return true;
	}

	// 텐서 로그
	UE_LOG(LogTemp, Log,
		TEXT("UDETeamWorldModel: Input tensors: %d"), InputDescs.Num());
	for (int32 i = 0; i < InputDescs.Num(); ++i)
	{
		const FTensorDesc& Desc = InputDescs[i];
		UE_LOG(LogTemp, Log,
			TEXT("  [%d] Name=%s, Rank=%d"),
			i, *Desc.GetName(), Desc.GetShape().Rank());
	}

	UE_LOG(LogTemp, Log,
		TEXT("UDETeamWorldModel: Output tensors: %d"), OutputDescs.Num());
	for (int32 i = 0; i < OutputDescs.Num(); ++i)
	{
		const FTensorDesc& Desc = OutputDescs[i];
		UE_LOG(LogTemp, Log,
			TEXT("  [%d] Name=%s, Rank=%d"),
			i, *Desc.GetName(), Desc.GetShape().Rank());
	}

	// 텐서 이름 저장
	if (InputDescs.Num() > 0)
	{
		InputTensorName = InputDescs[0].GetName();
	}
	if (OutputDescs.Num() > 0)
	{
		OutputTensorName = OutputDescs[0].GetName();
	}

	// 8. 더미 배치로 워밍업
	FDETeamBatchInput DummyInput;
	DummyInput.CurrentStates.Add(FDETeamWorldState());
	DummyInput.TacticalPlays.Add(ETacticalPlay::StandardComp);

	UE_LOG(LogTemp, Log,
		TEXT("UDETeamWorldModel: Performing warm-up inference..."));
	PredictBatch(DummyInput);

	bIsModelLoaded = true;

	UE_LOG(LogTemp, Log,
		TEXT("UDETeamWorldModel: Successfully initialized (Avg Latency: %.2fms)"),
		AverageLatencyMs);

	return true;
}


FDETeamBatchOutput UDETeamWorldModel::PredictBatch(const FDETeamBatchInput& BatchInput)
{
	using namespace UE::NNE;

	// Performance monitoring
	const double StartTime = FPlatformTime::Seconds();

	FDETeamBatchOutput Output;

	const uint32 BatchSize = BatchInput.CurrentStates.Num();
	if (BatchSize == 0 || BatchSize != BatchInput.TacticalPlays.Num())
	{
		UE_LOG(LogTemp, Error, TEXT("UDETeamWorldModel: Invalid batch input sizes"));
		return Output;
	}

	// 1. Preprocess: Convert FDETeamState + ETacticalPlay to tensor
	TArray<float> InputTensor = PreprocessBatch(BatchInput);

	TArray<float> OutputTensor;

	// 2. Run inference: NNE forward pass (if model loaded)
	if (bIsModelLoaded && ModelInstance.IsValid())
	{
		// Prepare input tensor binding
		constexpr int32 InputDim = 70; // 60-dim state + 10-dim tactical play one-hot
		constexpr int32 OutputDim = 64; // 60-dim next state + 3-dim reward + 1-dim confidence

		FTensorShape InputShape = FTensorShape::Make({BatchSize, InputDim});
		FTensorShape OutputShape = FTensorShape::Make({BatchSize, OutputDim});

		// Create tensor bindings
		TArray<FTensorBindingCPU> InputBindings;
		TArray<FTensorBindingCPU> OutputBindings;

		// Allocate output tensor
		OutputTensor.SetNumUninitialized(BatchSize * OutputDim);

		// Bind input/output
		InputBindings.Add(FTensorBindingCPU{InputTensor.GetData(), InputTensor.Num() * sizeof(float)});
		OutputBindings.Add(FTensorBindingCPU{OutputTensor.GetData(), OutputTensor.Num() * sizeof(float)});

		const auto SetShapeStatus = ModelInstance->SetInputTensorShapes({ InputShape });
		// Set input/output shapes
		if (SetShapeStatus != UE::NNE::IModelInstanceRunSync::ESetInputTensorShapesStatus::Ok)
		{
			UE_LOG(LogTemp, Error, TEXT("UDETeamWorldModel: Failed to set input tensor shape"));
			return Output;
		}

		// Run inference
		const auto RunResult = ModelInstance->RunSync(InputBindings, OutputBindings);
		if ((RunResult != UE::NNE::IModelInstanceRunSync::ERunSyncStatus::Ok))
		{
			UE_LOG(LogTemp, Error, TEXT("UDETeamWorldModel: Inference failed with code %d"), RunResult);
			// Fall back to stub mode
			OutputTensor.Init(0.0f, BatchSize * OutputDim);
		}
	}
	else
	{
		// Stub mode: Generate dummy predictions
		constexpr int32 OutputDim = 64;
		OutputTensor.SetNumUninitialized(BatchSize * OutputDim);

		// Fill with reasonable defaults for testing
		for (uint32 i = 0; i < BatchSize; ++i)
		{
			const uint32 BaseIdx = i * OutputDim;

			// Copy input state as predicted state (no change)
			const TArray<float> InputState = BatchInput.CurrentStates[i].ToTensor();
			for (int32 j = 0; j < 60 && j < InputState.Num(); ++j)
			{
				OutputTensor[BaseIdx + j] = InputState[j];
			}

			// Dummy rewards: neutral outcomes
			OutputTensor[BaseIdx + 60] = 0.5f;  // WinProb
			OutputTensor[BaseIdx + 61] = 0.0f;  // HealthDelta
			OutputTensor[BaseIdx + 62] = 0.0f;  // ObjectiveScore
			OutputTensor[BaseIdx + 63] = 0.1f;  // Low confidence (stub)
		}

		UE_LOG(LogTemp, Verbose, TEXT("UDETeamWorldModel: Running in stub mode (model not loaded)"));
	}

	// 3. Postprocess: Convert output tensor to FDETeamBatchOutput
	Output = PostprocessBatch(OutputTensor, BatchSize);

	// Update performance stats
	const double EndTime = FPlatformTime::Seconds();
	const float LatencyMs = static_cast<float>((EndTime - StartTime) * 1000.0);
	UpdateLatencyStats(LatencyMs);

	UE_LOG(LogTemp, Verbose, TEXT("UDETeamWorldModel: Predicted batch of %d samples in %.2fms"),
		BatchSize, LatencyMs);

	return Output;
}

void UDETeamWorldModel::PredictSingle(
	const FDETeamWorldState& CurrentState,
	ETacticalPlay TacticalPlay,
	FDETeamWorldState& OutNextState,
	FDECompositeReward& OutReward,
	float& OutConfidence)
{
	// Convenience wrapper for single prediction
	FDETeamBatchInput BatchInput;
	BatchInput.CurrentStates.Add(CurrentState);
	BatchInput.TacticalPlays.Add(TacticalPlay);

	FDETeamBatchOutput BatchOutput = PredictBatch(BatchInput);

	if (BatchOutput.PredictedStates.Num() > 0)
	{
		OutNextState = BatchOutput.PredictedStates[0];
		OutReward = BatchOutput.Rewards[0];
		OutConfidence = BatchOutput.Confidences[0];
	}
	else
	{
		// Return default values on failure
		OutNextState = FDETeamWorldState();
		OutReward = FDECompositeReward();
		OutConfidence = 0.0f;
	}
}

TArray<float> UDETeamWorldModel::PreprocessBatch(const FDETeamBatchInput& BatchInput)
{
	TArray<float> BatchTensor;
	const int32 BatchSize = BatchInput.CurrentStates.Num();

	// Get tactical play enum count for one-hot encoding
	// ETacticalPlay has ~10 values (AllOutRush, Phalanx, BaitStrategy, etc.)
	constexpr int32 TacticalPlayDim = 10;
	constexpr int32 StateDim = 60; // FDETeamState::ToTensor() output
	constexpr int32 InputDim = StateDim + TacticalPlayDim; // 70-dim total

	// Reserve space for efficiency: [BatchSize, 70]
	BatchTensor.Reserve(BatchSize * InputDim);

	for (int32 i = 0; i < BatchSize; ++i)
	{
		// 1. Convert FDETeamState to tensor (60-dim)
		TArray<float> StateTensor = BatchInput.CurrentStates[i].ToTensor();

		// Validate state tensor size
		if (StateTensor.Num() != StateDim)
		{
			UE_LOG(LogTemp, Error,
				TEXT("UDETeamWorldModel: Invalid state tensor size %d, expected %d"),
				StateTensor.Num(), StateDim);
			// Pad or truncate to expected size
			StateTensor.SetNum(StateDim);
		}

		BatchTensor.Append(StateTensor);

		// 2. One-hot encode tactical play (10-dim)
		// Example: ETacticalPlay::Phalanx (enum value 2) → [0,0,1,0,0,0,0,0,0,0]
		const int32 PlayIndex = static_cast<int32>(BatchInput.TacticalPlays[i]);
		for (int32 j = 0; j < TacticalPlayDim; ++j)
		{
			BatchTensor.Add(j == PlayIndex ? 1.0f : 0.0f);
		}
	}

	// Validate final tensor shape
	if (BatchTensor.Num() != BatchSize * InputDim)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("UDETeamWorldModel: Batch tensor size mismatch: %d vs expected %d"),
			BatchTensor.Num(), BatchSize * InputDim);
	}

	return BatchTensor;
}

FDETeamBatchOutput UDETeamWorldModel::PostprocessBatch(const TArray<float>& RawOutput, int32 BatchSize)
{
	FDETeamBatchOutput Output;
	Output.PredictedStates.Reserve(BatchSize);
	Output.Rewards.Reserve(BatchSize);
	Output.Confidences.Reserve(BatchSize);

	// Expected output format per sample:
	// - Next state: 60-dim (same structure as FDETeamState::ToTensor())
	// - Reward: 3-dim (WinProb, HealthDelta, ObjectiveScore)
	// - Confidence: 1-dim
	// Total: 64-dim per sample
	constexpr int32 OutputDimPerSample = 64;
	const int32 ExpectedSize = BatchSize * OutputDimPerSample;

	// Validate output tensor size
	if (RawOutput.Num() != ExpectedSize)
	{
		UE_LOG(LogTemp, Error,
			TEXT("UDETeamWorldModel: Output tensor size mismatch: %d vs expected %d"),
			RawOutput.Num(), ExpectedSize);

		// Return default values on error
		for (int32 i = 0; i < BatchSize; ++i)
		{
			Output.PredictedStates.Add(FDETeamWorldState());
			Output.Rewards.Add(FDECompositeReward());
			Output.Confidences.Add(0.0f);
		}
		return Output;
	}

	// Parse each sample in the batch
	for (int32 i = 0; i < BatchSize; ++i)
	{
		const int32 BaseIndex = i * OutputDimPerSample;

		// 1. Parse predicted next state (60-dim)
		FDETeamWorldState NextState = ParseTensorToTeamState(RawOutput, BaseIndex);
		Output.PredictedStates.Add(NextState);

		// 2. Parse reward (3-dim at offset 60)
		FDECompositeReward Reward;
		Reward.WinProb = FMath::Clamp(RawOutput[BaseIndex + 60], 0.0f, 1.0f);
		Reward.HealthDelta = FMath::Clamp(RawOutput[BaseIndex + 61], -1.0f, 1.0f);
		Reward.ObjectiveScore = FMath::Clamp(RawOutput[BaseIndex + 62], -1.0f, 1.0f);
		Output.Rewards.Add(Reward);

		// 3. Parse confidence (1-dim at offset 63)
		const float Confidence = FMath::Clamp(RawOutput[BaseIndex + 63], 0.0f, 1.0f);
		Output.Confidences.Add(Confidence);

		// Logging for debugging (verbose only)
		UE_LOG(LogTemp, VeryVerbose,
			TEXT("UDETeamWorldModel: Sample %d - WinProb=%.3f, HealthDelta=%.3f, Obj=%.3f, Conf=%.3f"),
			i, Reward.WinProb, Reward.HealthDelta, Reward.ObjectiveScore, Confidence);
	}

	return Output;
}

void UDETeamWorldModel::UpdateLatencyStats(float LatencyMs)
{
	// Simple exponential moving average
	const float Alpha = 0.1f;
	AverageLatencyMs = (AverageLatencyMs * (1.0f - Alpha)) + (LatencyMs * Alpha);
}

FDETeamWorldState UDETeamWorldModel::ParseTensorToTeamState(const TArray<float>& Tensor, int32 StartIndex) const
{
	FDETeamWorldState State;
	int32 Idx = StartIndex;

	// 1. Friendly positions (15-dim: 5 agents × 3D)
	State.FriendlyPositions.SetNum(5);
	for (int32 i = 0; i < 5; ++i)
	{
		State.FriendlyPositions[i] = FVector(
			Tensor[Idx++] * 10000.0f,  // Denormalize X
			Tensor[Idx++] * 10000.0f,  // Denormalize Y
			Tensor[Idx++] * 1000.0f    // Denormalize Z
		);
	}

	// 2. Friendly health (5-dim)
	State.FriendlyHealths.SetNum(5);
	for (int32 i = 0; i < 5; ++i)
	{
		State.FriendlyHealths[i] = FMath::Clamp(Tensor[Idx++], 0.0f, 1.0f);
	}

	// 3. Friendly cooldowns (5-dim)
	State.FriendlyCooldowns.SetNum(5);
	for (int32 i = 0; i < 5; ++i)
	{
		State.FriendlyCooldowns[i] = FMath::Clamp(Tensor[Idx++], 0.0f, 1.0f);
	}

	// 4. Friendly alive (5-dim)
	State.FriendlyAlive.SetNum(5);
	for (int32 i = 0; i < 5; ++i)
	{
		State.FriendlyAlive[i] = Tensor[Idx++] > 0.5f;
	}

	// 5. Friendly strategies (5-dim)
	State.FriendlyStrategies.SetNum(5);
	for (int32 i = 0; i < 5; ++i)
	{
		const int32 StrategyValue = FMath::RoundToInt(Tensor[Idx++]);
		State.FriendlyStrategies[i] = static_cast<EDEStrategyType>(FMath::Clamp(StrategyValue, 0, 2));
	}

	// 6. Enemy positions (15-dim: 5 enemies × 3D)
	State.EnemyPositions.SetNum(5);
	for (int32 i = 0; i < 5; ++i)
	{
		State.EnemyPositions[i] = FVector(
			Tensor[Idx++] * 10000.0f,
			Tensor[Idx++] * 10000.0f,
			Tensor[Idx++] * 1000.0f
		);
	}

	// 7. Enemy confidences (5-dim)
	State.EnemyConfidences.SetNum(5);
	for (int32 i = 0; i < 5; ++i)
	{
		State.EnemyConfidences[i] = FMath::Clamp(Tensor[Idx++], 0.0f, 1.0f);
	}

	// 8. Enemy health (5-dim)
	State.EnemyHealths.SetNum(5);
	for (int32 i = 0; i < 5; ++i)
	{
		State.EnemyHealths[i] = FMath::Clamp(Tensor[Idx++], 0.0f, 1.0f);
	}

	// 9. Enemy alive (5-dim)
	State.EnemyAlive.SetNum(5);
	for (int32 i = 0; i < 5; ++i)
	{
		State.EnemyAlive[i] = Tensor[Idx++] > 0.5f;
	}

	// 10. Capture point ownership (5-dim)
	State.CapturePointOwnership.SetNum(5);
	for (int32 i = 0; i < 5; ++i)
	{
		State.CapturePointOwnership[i] = FMath::RoundToInt(Tensor[Idx++]);
	}


	// 12. Time remaining (1-dim)
	State.TimeRemaining = FMath::Clamp(Tensor[Idx++], 0.0f, 1.0f);

	return State;
}
