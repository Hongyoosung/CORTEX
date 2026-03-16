// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "Policies/NNEPolicy.h"
#include "Misc/ScopeExit.h"
#include "NNE.h"
#include "NNERuntimeCPU.h"
#include "NNERuntimeGPU.h"
#include "NNERuntimeRunSync.h"
#include "NNEStatus.h"
#include "NNEUtils/NNEPointToBufferConverter.h"
#include "NNEUtils/NNEBufferAllocator.h"
#include "NNEUtils/NNEPointCreator.h"
#include "NNEUtils/NNEBindingCreator.h"
#include "LogScholaNNE.h"


TArray<FString> UNNEPolicy::GetRuntimeNames() const
{
	// we don't support RDG yet so skip it here
	// TArray < FString> ValidRuntimes = UE::NNE::GetAllRuntimeNames < INNERuntimeCPU>();
	// ValidRuntimes.Append(UE::NNE::GetAllRuntimeNames<INNERuntimeGPU>());
	return UE::NNE::GetAllRuntimeNames();
}

IRuntimeInterface* UNNEPolicy::GetRuntime(const FString& SelectedRuntimeName) const
{
	TWeakInterfacePtr<INNERuntimeCPU> CPUPtr = UE::NNE::GetRuntime<INNERuntimeCPU>(SelectedRuntimeName);
	if (CPUPtr.IsValid())
	{
		return new FCPURuntimeWrapper(CPUPtr);
	}

	TWeakInterfacePtr<INNERuntimeGPU> GPUPtr = UE::NNE::GetRuntime<INNERuntimeGPU>(SelectedRuntimeName);
	if (GPUPtr.IsValid())
	{
		return new FGPURuntimeWrapper(GPUPtr);
	}
	// Should probably never happen but
	return nullptr;
}


bool UNNEPolicy::Think(const TInstancedStruct<FPoint>& InObservations, TInstancedStruct<FPoint>& OutAction)
{
	bool Expected = false;
	if (!bInferenceInFlight.compare_exchange_weak(Expected, true, std::memory_order_acq_rel))
	{
		UE_LOG(LogScholaNNE, Verbose, TEXT("NNEPolicy::Think() - Inference already in Flight"));
		return false;
	}

	// Scope guard to reset inference flag
	ON_SCOPE_EXIT
	{
		bInferenceInFlight.store(false, std::memory_order_release);
	};

	// If the network hasn't been initialized, bail immediately
	if (!bNetworkLoaded)
	{
		UE_LOG(LogScholaNNE, Display, TEXT("NNEPolicy::Think() - Network not loaded, returning false"));
		return false;
	}
    
	// Initialize the OutAction container lazily, but only after initialization
	if(!OutAction.IsValid())
	{
		if (!ActionBuffer.IsValid() || !PolicyDefinition.ActionSpaceDefn.IsValid())
		{
			UE_LOG(LogScholaNNE, Error, TEXT("NNEPolicy::Think() - ActionBuffer or ActionSpace is invalid after init"));
			return false;
		}
		// Initialize the OutAction with the ActionSpace Definition
		FNNEPointCreator::CreatePoint(ActionBuffer, OutAction, PolicyDefinition.ActionSpaceDefn);
	}
	
	FNNEPointToBufferConverter::ConvertPointToBuffer(InObservations, ObservationBuffer, PolicyDefinition.ObsSpaceDefn);
	
	if((int)ModelInstance->RunSync(InputBindings, OutputBindings) != 0)
	{
		UE_LOG(LogScholaNNE, Warning, TEXT("NNEPolicy::Think() - Model inference failed"));
		return false;
	}

	// Copy the Buffer into the pre-allocated ActionPoint
	FNNEPointCreator::CreatePoint(ActionBuffer, OutAction, PolicyDefinition.ActionSpaceDefn);	
	return true;
}

bool UNNEPolicy::InitInputTensorShapes(TSharedPtr<IModelInstanceRunSync> InModelInstance)
{
	TArray<UE::NNE::FTensorShape> TempShapeArray;

	for (const UE::NNE::FTensorDesc& TensorDesc : InModelInstance->GetInputTensorDescs())
	{
		TempShapeArray.Add(UE::NNE::FTensorShape::MakeFromSymbolic(TensorDesc.GetShape()));
	}

	return UE::NNE::IModelInstanceRunSync::ESetInputTensorShapesStatus::Ok == InModelInstance->SetInputTensorShapes(TempShapeArray);
}

bool UNNEPolicy::InitStateBuffersAndBindings(TSharedPtr<IModelInstanceRunSync> InModelInstance)
{
	// Find all the State Tensors from the Tensor Descriptions and Create a buffer for each one
	TConstArrayView<UE::NNE::FTensorDesc> InputTensorDescs = InModelInstance->GetInputTensorDescs(); 
    for (int i = 0; i < InputTensorDescs.Num(); i++)
	{
        if(InputTensorDescs[i].GetName().StartsWith(TEXT("state_in")))
        {
            UE::NNE::FTensorDesc StateDesc = InputTensorDescs[i];
			if (StateDesc.GetShape().Rank() != 3)
			{
				UE_LOG(LogScholaNNE, Error, TEXT("Invalid input tensor shape for state buffer, should have rank 3"));
				return false;
			}
			int StateSeqLen = StateDesc.GetShape().GetData()[1];
			int StateDimSize = StateDesc.GetShape().GetData()[2];
			FNNEStateBuffer& BufferRef = this->StateBuffer.Emplace_GetRef(StateSeqLen, StateDimSize);
			this->InputBindings[i] = BufferRef.MakeInputBinding();
        }
	}
	// Go through the output tensors and find the state tensors, linking them to the state buffer for the corresponding input tensor
	TConstArrayView<UE::NNE::FTensorDesc> OutputTensorDescs = InModelInstance->GetOutputTensorDescs();
	int StateIndex = 0;
	for (int i = 0; i < OutputTensorDescs.Num(); i++)
	{
        if(OutputTensorDescs[i].GetName().StartsWith(TEXT("state_out")))
        {
			FNNEStateBuffer& BufferRef = this->StateBuffer[StateIndex++];
			this->OutputBindings[i] = BufferRef.MakeOutputBinding();
			//StateIndex++; // double increment?
        }
	}
	return true;
}

void UNNEPolicy::InitNonStateBuffers(const FInteractionDefinition& InDefinition)
{
	FNNEBufferAllocator::AllocateBuffer(InDefinition.ObsSpaceDefn, this->ObservationBuffer);
	FNNEBufferAllocator::AllocateBuffer(InDefinition.ActionSpaceDefn, this->ActionBuffer);
}

bool UNNEPolicy::InitNonStateBindings(const FInteractionDefinition& InDefinition)
{
	//Observation Bindings
	if (FNNEBindingCreator::CreateBindings(InDefinition.ObsSpaceDefn, this->ObservationBuffer, ModelInstance->GetInputTensorDescs(), this->InputBindings))
	{
		UE_LOG(LogScholaNNE, Error, TEXT("Failed to create bindings for Observation Space"));
		return false;
	}
	
	//Action Bindings
	if (FNNEBindingCreator::CreateBindings(InDefinition.ActionSpaceDefn, this->ActionBuffer, ModelInstance->GetOutputTensorDescs(), this->OutputBindings))
	{
		UE_LOG(LogScholaNNE, Error, TEXT("Failed to create bindings for Action Space"));
		return false;
	}
	return true;
}

bool UNNEPolicy::AllocateBindingArrays(TSharedPtr<IModelInstanceRunSync> InModelInstance)
{
	InputBindings.Init({ nullptr, 0 }, InModelInstance->GetInputTensorDescs().Num());
	OutputBindings.Init({ nullptr, 0 }, InModelInstance->GetOutputTensorDescs().Num());

	if (InputBindings.Num() == 0 || OutputBindings.Num() == 0)
	{
		UE_LOG(LogScholaNNE, Error, TEXT("No Input or Output Bindings found for the Model. Must be atleast one input and one output binding for the model to run"));
		return false;
	}

	return true;
}

bool UNNEPolicy::SetupBuffersAndBindings(const FInteractionDefinition& InDefinition, TSharedPtr<IModelInstanceRunSync> InModelInstance)
{
	// Add Empty Entries to the Input and Output Bindings, so we can update the entries instead of Allocating later.
	if(!AllocateBindingArrays(InModelInstance))
	{
		UE_LOG(LogScholaNNE, Error, TEXT("Failed to allocate input or output bindings"));
		return false;
	}

	// Allocate memory for the buffers corresponding to Actions and Observations
	InitNonStateBuffers(InDefinition);

	// Prepare buffers and bindings for state tensors, e.g. as used in RNNs or LSTM networks
	if (!InitStateBuffersAndBindings(InModelInstance))
	{
		UE_LOG(LogScholaNNE, Error, TEXT("Failed to initialize state buffers"));
		return false;
	}

	// Bind the remaining inputs and outputs to the Action and Observation buffers
	InitNonStateBindings(InDefinition);

	if(!InitInputTensorShapes(InModelInstance))
	{
		UE_LOG(LogScholaNNE, Error, TEXT("Failed to initialize input tensor shapes"));
		return false;
	}

	return true;
}


bool UNNEPolicy::Init(const FInteractionDefinition& InPolicyDefinition)
{
	// Store the policy definition for later use
	this->PolicyDefinition = InPolicyDefinition;

	if (!ModelData)
	{
		UE_LOG(LogScholaNNE, Warning, TEXT("Failed to Create Network Due to Invalid Model Data"));
		// Invalid Model Data
		bNetworkLoaded = false;
		return false;
	}

	// Create and store runtime in the class member to avoid shadowing and ensure lifetime
	this->Runtime = TUniquePtr<IRuntimeInterface>(this->GetRuntime(this->RuntimeName));
	if (!this->Runtime.IsValid() || !this->Runtime->IsValid())
	{
		UE_LOG(LogScholaNNE, Error, TEXT("Cannot find runtime %s, please enable the corresponding plugin"), *this->RuntimeName);
		// Invalid Runtime
		bNetworkLoaded = false;
		return false;
	}

	TUniquePtr<IModelInterface> TempModelPtr = this->Runtime->CreateModel(ModelData);
	if (!TempModelPtr.IsValid() || !TempModelPtr->IsValid())
	{
		UE_LOG(LogScholaNNE, Warning, TEXT("Failed to Create the Model"));
		// Invalid Runtime
		bNetworkLoaded = false;
		return false;
	}

	// Store the model in the class member so it outlives the instance
	this->Model = MoveTemp(TempModelPtr);

	ModelInstance = this->Model->CreateModelInstance();
	if (!ModelInstance.IsValid())
	{
		UE_LOG(LogScholaNNE, Error, TEXT("Failed to create the model instance"));
		bNetworkLoaded = false;
		return false;
	}

	if (!this->SetupBuffersAndBindings(InPolicyDefinition, ModelInstance))
	{
		UE_LOG(LogScholaNNE, Error, TEXT("Failed to setup buffers and bindings"));
		bNetworkLoaded = false;
		return false;
	}

	bNetworkLoaded = true;
	return true;
}

bool UNNEPolicy::IsInferenceBusy() const
{
	return bInferenceInFlight.load(std::memory_order_relaxed);
}
