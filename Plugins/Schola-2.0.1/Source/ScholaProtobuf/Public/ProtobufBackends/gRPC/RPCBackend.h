// Copyright (c) 2023 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "grpcpp/server.h"

using grpc::Server;
using grpc::ServerCompletionQueue;
using grpc::ServerAsyncResponseWriter;


// This basically functions as a dataclass with most functionality being in children or calldata
template <class ServiceType, typename RequestType, typename ResponseType>
class TRPCBackend
{

protected:

	std::unique_ptr<ServerCompletionQueue> _CQueue;
	std::shared_ptr<ServiceType>		   Service;
	std::unique_ptr<Server>				   Server;
	
	/** The signature of the handler for an asynchronous RPC, that is handled by this CallData. */
	using AsyncRPCHandle = void (ServiceType::*)(grpc::ServerContext* context,
		RequestType*												 request,
		ServerAsyncResponseWriter<ResponseType>*					 response,
		grpc::CompletionQueue*										 new_call_cq,
		ServerCompletionQueue*										 notification_cq,
		void*														 tag);
	
	AsyncRPCHandle TargetRPC;

public:
	TRPCBackend(AsyncRPCHandle TargetRPC, std::shared_ptr<ServiceType> Service, std::unique_ptr<ServerCompletionQueue> CQueue)
	{
		this->TargetRPC = TargetRPC;
		this->Service = Service;
		this->_CQueue = std::move(CQueue);
	}

	void SetCompletionQueue(std::unique_ptr<ServerCompletionQueue> CQueue)
	{
		this->_CQueue = CQueue;
	}
};