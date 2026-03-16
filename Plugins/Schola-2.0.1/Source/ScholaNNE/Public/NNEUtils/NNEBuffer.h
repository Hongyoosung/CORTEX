// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "NNE.h"
#include "StructUtils/InstancedStruct.h"
#include <NNERuntimeRunSync.h>
#include "NNEBuffer.generated.h"


/**
 * @brief Buffer for storing recurrent neural network state across time steps
 * 
 * This buffer maintains state information for sequence-based neural network models,
 * storing a sequence of state vectors that can be shifted and updated.
 */
USTRUCT(BlueprintType)
struct SCHOLANNE_API FNNEStateBuffer
{
	GENERATED_BODY()

	/** Buffer storing the sequence of state vectors */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Policy Data")
	TArray<float> StateBuffer;

	/** Length of the state sequence */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Policy Properties")
	int StateSeqLen;

	/** Dimensionality of each state vector */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="Policy Properties")
	int StateDimSize;

	/**
	 * @brief Constructor that initializes the state buffer
	 * @param[in] InStateSeqLen Length of the state sequence
	 * @param[in] InStateDimSize Dimensionality of each state vector
	 */
	FNNEStateBuffer(int InStateSeqLen = 0, int InStateDimSize = 0) : 
		StateSeqLen(InStateSeqLen), 
		StateDimSize(InStateDimSize)
	{
		this->StateBuffer.Init(0.0f, InStateSeqLen * InStateDimSize);
	}
	
	/**
	 * @brief Shifts the state sequence by removing the oldest state and making room for a new one
	 * 
	 * Moves all state vectors one position earlier in the sequence, discarding the first state
	 * and preparing the last position for a new state.
	 */
	void Shift()
	{
		// Shift overlapping regions safely
		for (int i = 0; i < StateSeqLen - 1; i++)
		{
			FMemory::Memmove(
				StateBuffer.GetData() + i * StateDimSize,
				StateBuffer.GetData() + (i + 1) * StateDimSize,
				StateDimSize * sizeof(float));
		}
	}

	/**
	 * @brief Creates an NNE tensor binding for the entire state buffer as input
	 * @return Tensor binding containing the full state sequence
	 */
	UE::NNE::FTensorBindingCPU MakeInputBinding() const
	{
		return { (void*)(StateBuffer.GetData()), StateSeqLen * StateDimSize * sizeof(float) };
	}

	/**
	 * @brief Creates an NNE tensor binding for the last state vector as output
	 * @return Tensor binding pointing to the most recent state position
	 */
	UE::NNE::FTensorBindingCPU MakeOutputBinding() const
	{
		return { (void*)(StateBuffer.GetData() + (StateSeqLen - 1) * StateDimSize), StateDimSize * sizeof(float) };
	}


};

struct FNNEBufferVisitor;

/**
 * @brief Base class for all NNE point buffers
 * 
 * This is an abstract base class that provides the visitor pattern interface for
 * different types of buffers used to store neural network inputs and outputs.
 */
USTRUCT(BlueprintType)
struct SCHOLANNE_API FNNEPointBuffer
{
	GENERATED_BODY()

public:
	virtual ~FNNEPointBuffer() = default;

	/**
	 * @brief Accept method for the visitor pattern
	 * @param[in,out] Visitor The visitor object that will process this buffer
	 */
	virtual void Accept(FNNEBufferVisitor& Visitor) const 
	{

	}
};

/**
 * @brief Buffer for dictionary-structured neural network data
 * 
 * Stores multiple named buffers organized as key-value pairs, corresponding to
 * dictionary observation or action spaces in reinforcement learning.
 */
USTRUCT(BlueprintType)
struct SCHOLANNE_API FNNEDictBuffer : public FNNEPointBuffer
{	
	GENERATED_BODY(BlueprintType)

	/** Map of named buffers, each corresponding to a dictionary key */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Policy Data")
	TMap<FString, TInstancedStruct<FNNEPointBuffer>> Buffers;

	virtual ~FNNEDictBuffer() = default;

	FNNEDictBuffer() = default;

	/**
	 * @brief Constructor that initializes the dictionary buffer with a map of buffers
	 * @param[in] InBuffers Map of buffer names to buffer instances
	 */
	FNNEDictBuffer(const TMap<FString, TInstancedStruct<FNNEPointBuffer>>& InBuffers)
		: Buffers(InBuffers)
	{
	}

	/**
	 * @brief Accept method for the visitor pattern
	 * @param[in,out] Visitor The visitor object that will process this buffer
	 */
	void Accept(FNNEBufferVisitor& Visitor) const override;
};

/**
 * @brief Buffer for discrete action or observation spaces
 * 
 * Stores a single discrete choice represented as a probability distribution over possible actions.
 */
USTRUCT(BlueprintType)
struct SCHOLANNE_API FNNEDiscreteBuffer : public FNNEPointBuffer
{	
	GENERATED_BODY()

	/** Buffer storing the discrete value as a one-hot encoded or probability distribution */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Policy Data")
	TArray<float> Buffer;

	FNNEDiscreteBuffer() = default;

	/**
	 * @brief Constructor that initializes the discrete buffer with a specific size
	 * @param[in] Size Number of discrete values or action possibilities
	 */
	FNNEDiscreteBuffer(int Size)
	{
		Buffer.Init(0.0f, Size);
	}

	virtual ~FNNEDiscreteBuffer() = default;

	/**
	 * @brief Creates an NNE tensor binding for this buffer
	 * @return Tensor binding containing the buffer data
	 */
	UE::NNE::FTensorBindingCPU MakeBinding() const
	{
		return { (void*) Buffer.GetData(), Buffer.Num() * sizeof(float) };
	}

	/**
	 * @brief Accept method for the visitor pattern
	 * @param[in,out] Visitor The visitor object that will process this buffer
	 */
	void Accept(FNNEBufferVisitor& Visitor) const override;
};

/**
 * @brief Buffer for multi-discrete action or observation spaces
 * 
 * Stores multiple discrete choices, where each choice can have different numbers of possible values.
 * Used when an agent makes several independent discrete decisions simultaneously.
 */
USTRUCT(BlueprintType)
struct SCHOLANNE_API FNNEMultiDiscreteBuffer : public FNNEPointBuffer
{	
	GENERATED_BODY()

	/** Buffer storing concatenated probability distributions for all discrete choices */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Policy Data")
	TArray<float> Buffer;

	FNNEMultiDiscreteBuffer() = default;

	/**
	 * @brief Constructor that initializes the multi-discrete buffer with a specific size
	 * @param[in] Size Total size of all discrete distributions combined
	 */
	FNNEMultiDiscreteBuffer(int Size)
	{
		Buffer.Init(0.0f, Size);
	}

	virtual ~FNNEMultiDiscreteBuffer() = default;

	/**
	 * @brief Creates an NNE tensor binding for this buffer
	 * @return Tensor binding containing the buffer data
	 */
	UE::NNE::FTensorBindingCPU MakeBinding() const
	{
		return { (void*) Buffer.GetData(), Buffer.Num() * sizeof(float) };
	}

	/**
	 * @brief Accept method for the visitor pattern
	 * @param[in,out] Visitor The visitor object that will process this buffer
	 */
	void Accept(FNNEBufferVisitor& Visitor) const override;
};



/**
 * @brief Buffer for multi-binary action or observation spaces
 * 
 * Stores multiple independent binary values, where each element represents a binary choice (on/off).
 * Common in scenarios with multiple simultaneous boolean decisions.
 */
USTRUCT(BlueprintType)
struct SCHOLANNE_API FNNEMultiBinaryBuffer : public FNNEPointBuffer
{
	GENERATED_BODY()
	
	/** Buffer storing multiple binary values as floats (0.0 or 1.0) */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Policy Data")
	TArray<float> Buffer;

public:

	FNNEMultiBinaryBuffer() = default;

	/**
	 * @brief Constructor that initializes the multi-binary buffer with a specific size
	 * @param[in] Size Number of binary values
	 */
	FNNEMultiBinaryBuffer(int Size)
	{
		Buffer.Init(0.0f, Size);
	}

	virtual ~FNNEMultiBinaryBuffer() = default;

	/**
	 * @brief Creates an NNE tensor binding for this buffer
	 * @return Tensor binding containing the buffer data
	 */
	UE::NNE::FTensorBindingCPU MakeBinding() const
	{
		return {(void*)Buffer.GetData(), Buffer.Num() * sizeof(float) };
	}

	/**
	 * @brief Accept method for the visitor pattern
	 * @param[in,out] Visitor The visitor object that will process this buffer
	 */
	void Accept(FNNEBufferVisitor& Visitor) const override;
};

/**
 * @brief Buffer for continuous box-bounded action or observation spaces
 * 
 * Stores continuous values within bounded ranges, commonly used for continuous control
 * tasks where actions or observations are real-valued vectors with upper and lower bounds.
 */
USTRUCT(BlueprintType)
struct SCHOLANNE_API FNNEBoxBuffer : public FNNEPointBuffer
{	
	GENERATED_BODY()

	/** Buffer storing continuous values */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Policy Data")
	TArray<float> Buffer;

public:
	FNNEBoxBuffer() = default;
	virtual ~FNNEBoxBuffer() = default;

	/**
	 * @brief Constructor that initializes the box buffer with a specific size
	 * @param[in] Size Dimensionality of the continuous space
	 */
	FNNEBoxBuffer(int Size)
	{
		Buffer.Init(0.0f, Size);
	}

	/**
	 * @brief Creates an NNE tensor binding for this buffer
	 * @return Tensor binding containing the buffer data
	 */
	UE::NNE::FTensorBindingCPU MakeBinding() const
	{
		return {(void*)Buffer.GetData(), Buffer.Num() * sizeof(float)};
	}

	/**
	 * @brief Accept method for the visitor pattern
	 * @param[in,out] Visitor The visitor object that will process this buffer
	 */
	void Accept(FNNEBufferVisitor& Visitor) const override;
};
