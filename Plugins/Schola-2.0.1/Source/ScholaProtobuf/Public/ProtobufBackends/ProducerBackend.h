// Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once
#include "ProtobufBackends/ProtobufBackend.h"
#include "ProtobufUtils/ProtobufSerializer.h"


/**
 * @brief A Generic Interface for any service that can be used to send messages of type T asynchronously
 * @tparam T The type of message that will be input to this interface
 */
template <typename T>
class IProducerBackend : public IProtobufBackend
{
public:

	/**
	 * @brief Send a message to the client
	 * @param[in] Msg The message to send to the client
	 */
	virtual void Publish(T* Msg) = 0;
    
    virtual T* Allocate()
    {
        return new T();
    }

	template <typename UnrealType>
	void Publish(const UnrealType& InUnrealValue)
	{
		T* ProtoMsg = Allocate();
		ProtobufSerializer::ToProto(InUnrealValue, ProtoMsg);
		this->Publish(ProtoMsg);
	}
};
