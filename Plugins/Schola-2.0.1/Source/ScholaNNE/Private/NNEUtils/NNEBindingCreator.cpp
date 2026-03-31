// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#include "NNEUtils/NNEBindingCreator.h"
#include "LogScholaNNE.h"


//TODO this needs some error handling for when things don't line up properly, e.g. if the space is not found in the buffer map.
//Note this can happen for Stateful models, since those will have IO for state that isn't in the space. passed for binding
// ~acann, 7/4/2025

void FNNEBindingCreator::operator()(const FNNEDictBuffer& InBuffer)
{
    for (; this->Index < TensorDescs.Num(); this->Index++)
	{

        if(TensorDescs[this->Index].GetName().StartsWith(TEXT("state_in")) 
            || TensorDescs[this->Index].GetName().StartsWith(TEXT("state_out")))
        {
            // Do nothing as we will handle state buffers separately
        }
		else if (this->Index >= Bindings.Num())
		{
			//UE_LOG("LogSchola", Error, TEXT("NNE Binding Creator: Index %d out of bounds for Bindings array of size %d"), this->Index, Bindings.Num());
			this->bError = true;
			return;
		}
        else
        {
			const TInstancedStruct<FNNEPointBuffer>* PointBuffer = InBuffer.Buffers.Find(TensorDescs[this->Index].GetName());

			if (PointBuffer && PointBuffer->IsValid())
			{
				PointBuffer->Get<FNNEPointBuffer>().Accept(*this);
			}
			else
			{
				UE_LOG(LogScholaNNE, Error, TEXT("NNE Binding Creator: Key %s not found or invalid in Dict Buffer"), *TensorDescs[this->Index].GetName());
				this->bError = true;
				return;
			}
        }
	}

}

void FNNEBindingCreator::operator()(const FNNEBoxBuffer& InBuffer)
{
	Bindings[this->Index] = InBuffer.MakeBinding();
}

void FNNEBindingCreator::operator()(const FNNEMultiBinaryBuffer& InBuffer)
{
	Bindings[this->Index] = InBuffer.MakeBinding();
}

void FNNEBindingCreator::operator()(const FNNEDiscreteBuffer& InBuffer)
{
	Bindings[this->Index] = InBuffer.MakeBinding();
}

void FNNEBindingCreator::operator()(const FNNEMultiDiscreteBuffer& InBuffer)
{
	Bindings[this->Index] = InBuffer.MakeBinding();
}
