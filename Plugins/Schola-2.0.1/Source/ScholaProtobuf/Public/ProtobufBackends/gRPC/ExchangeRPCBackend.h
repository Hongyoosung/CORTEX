// Copyright (c) 2023-2025 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Async/Future.h"
#include "Containers/Queue.h"
#include "./CallData.h"
#include "./RPCBackend.h"
#include <grpcpp/server.h>
#include "ProtobufUtils/ProtobufSerializer.h"
#include "ProtobufUtils/ProtobufDeserializer.h"
#include "ProtobufBackends/ExchangeBackend.h"


template <class ServiceType, typename RequestType, typename ResponseType>
class TExchangeCallData : public TCallData<ServiceType, RequestType, ResponseType>
{
protected:
	TPromise<const RequestType*> RequestPromise;
	using CallData = TCallData<ServiceType, RequestType, ResponseType>;

public:
	bool bHasRequest = false;
	bool bIsFirst = false;

	TExchangeCallData(ServiceType* Service, ServerCompletionQueue* CQueue, CallData::AsyncAPIHandler TargetRPC, bool bIsFirst = false)
		: CallData(Service, CQueue, TargetRPC, false, false), bIsFirst(bIsFirst)
	{
		// Note we disable autocreate here so that we can do some work first before creating the new calldata objects
	}

	/**
	 * @brief Get a future representing an eventual request from a client
	 * @return A Future that will be fulfilled once a RPC is initiated
	 */
	TFuture<const RequestType*> GetRequestFuture()
	{
		return RequestPromise.GetFuture();
	}

	/**
	 * @brief Send a request from a client to any consumers in Unreal
	 */
	void FulfillRequestPromise()
	{
		// Note there is an edge case that can cause nullptr exceptions, but doesn't occur in practice:
		//  1. we fulfill a request 2. server is shutdown 3. calldata is cleaned up by worker thread
		//  4. user access the pointer which now references nothing
		// One fix is to have this as a ptr instead of a member leaving it up to the user
		// to handle deallocation.
		//  Leaving this is okay for now because this only happens when the server shuts down which
		//  we don't promise to handle and continue.
		this->RequestPromise.EmplaceValue(&this->Request);
	}

	/**
	 * @brief Send a default value to the Unreal-side consumer.
	 * used to handle edge cases where two exchanges are made in a row
	 */
	void DefaultOnRequestPromise()
	{
		// make a default object of RequestType and pray
		this->RequestPromise.GetFuture().Reset();
		TSharedFuture<const RequestType*> OldFuture = this->RequestPromise.GetFuture().Share();

		this->RequestPromise.EmplaceValue(new RequestType());
	}
	void Fail()
	{
		// We just die and then wait until we get vomitted onto the completion queue
		this->DefaultOnRequestPromise();
	}

	/**
	 * @brief Is this RPC ready to be completed.
	 */
	bool CanSubmit()
	{
		return this->Response != nullptr;
	}
};

// A debug int for tracking logs across Exchanges
static int ExchangeID = 0;

template <class ServiceType, typename RequestType, typename ResponseType>
class ExchangeRPCWorker : public FRunnable
{
private:
	typedef TExchangeCallData<ServiceType, RequestType, ResponseType> _ExchCallData;
	// CQueue owned by this workers parent
	ServerCompletionQueue* CQueue;
	// The ID of this worker
	int LocalID;

public:
	// The thread object this worker runs on
	FRunnableThread* Thread = nullptr;

	ExchangeRPCWorker(ServerCompletionQueue* CQueue, int ID)
	{
		LocalID = ID;
		this->CQueue = CQueue;
	}

	~ExchangeRPCWorker()
	{
		delete Thread;
	}
	/**
	 * @brief Initialize the worker
	 * @return True. Since initialization cannot fail
	 */
	virtual bool Init()
	{
		// Do Nothing here
		return true;
	}

	/**
	 * @brief This workers main method
	 * @return A status code representing the result
	 */
	virtual uint32 Run()
	{
		// This thread will loop through and fulfill promises etc on the exchange server
		void* tag = nullptr; // uniquely identifies a request.
		bool  ok = true;
		while (true)
		{
			bool Status = CQueue->Next(&tag, &ok);
			// Gotta check this way because if the queue was empty we also get a nonsense tag
			if (!Status)
			{
				// Queue drained so we can exit
				UE_LOG(LogScholaCommunicator, Verbose, TEXT("Exchange Queue %d Drained and Shutdown"), LocalID);
				return -1;
			}
			else if (!ok)
			{
				// we can assume this since other events will have a tag
				// if we get nullptr and !ok then the Queue must be empty and therefore Status=False

				// This tag was cleanupable so clean it up
				if (tag != nullptr)
				{
					
					UE_LOG(LogScholaCommunicator, Verbose, TEXT("Bad Event in Exchange Queue %d, cleaning up the tag"), LocalID);
					_ExchCallData* CallData = static_cast<_ExchCallData*>(tag);
					if (CallData->HasResponse())
					{
						UE_LOG(LogScholaCommunicator, VeryVerbose, TEXT("Bad Event was Message: %s"), *FString(CallData->GetRequest().DebugString().c_str()));
					}
					CallData->CleanUp();
				}
				else
				{
					UE_LOG(LogScholaCommunicator, Warning, TEXT("Empty Event in Exchange Queue %d. How did you get here? The Queue should be empty in this case."), LocalID);
				}
			}
			else
			{
				_ExchCallData* CallData = static_cast<_ExchCallData*>(tag);

				if (CallData->IsReady())
				{
					if (CallData->HasResponse())
					{
						UE_LOG(LogScholaCommunicator, VeryVerbose, TEXT("Message in Exchange Queue %d,: %s"), LocalID, *FString(CallData->GetRequest().DebugString().c_str()));
					}
					// fulfill the request promise but don't put it back on the queue
					// Note we will never double fullfill because we don't get back on the queue until we are out of process state
					CallData->FulfillRequestPromise();
					CallData->bHasRequest = true;
				}
				else
				{
					CallData->DoWork();
				}
			}
		}
	}

	/**
	 * @brief Start the worker
	 */
	void Start()
	{
		UE_LOG(LogScholaCommunicator, Verbose, TEXT("Starting Exchange Worker %d"), LocalID);
		Thread = FRunnableThread::Create(this, TEXT("ExchangeRPCWorker"), 0, TPri_Normal);
	}

	/**
	 * @brief Shutdown the worker and it's associated completion queue
	 */
	virtual void Stop()
	{
		UE_LOG(LogScholaCommunicator, Verbose, TEXT("Shutting Down Exchange Queue %d"), LocalID);
		CQueue->Shutdown();
		// Wait for the CQueue to drain
		if (Thread != nullptr)
		{
			Thread->WaitForCompletion();
		}
	}

	/**
	 * @brief Unused. Called when the thread completes
	 */
	virtual void Exit()
	{
		// Called on Completion so do nothing
	}
};

template <class ServiceType, typename RequestType, typename ResponseType>
class TExchangeRPCBackend : public TRPCBackend<ServiceType, RequestType, ResponseType>, public IExchangeBackend<RequestType, ResponseType>
{
private:
	int																 LocalID = 0;
	using CallData = TExchangeCallData<ServiceType, RequestType, ResponseType>;
	// Note these are inverted since we are sending response, before the request arrives from gRPC perspective
	CallData*											   CurrExchange = nullptr;
	ExchangeRPCWorker<ServiceType, RequestType, ResponseType>* Worker;
	int														   MsgID = 0;
	using gRPCBackend = TRPCBackend<ServiceType, RequestType, ResponseType>;

public:
	TExchangeRPCBackend(gRPCBackend::AsyncRPCHandle TargetRPC, std::shared_ptr<ServiceType> Service, std::unique_ptr<ServerCompletionQueue> CQueue)
		: gRPCBackend(TargetRPC, Service, std::move(CQueue))
	{
		LocalID = ExchangeID++;
		this->Worker = new ExchangeRPCWorker<ServiceType, RequestType, ResponseType>(gRPCBackend::_CQueue.get(), LocalID);
	}

	~TExchangeRPCBackend()
	{
		UE_LOG(LogScholaCommunicator, Warning, TEXT("Manually Deleting ExchangeRPC Backend %d"), LocalID);
		Shutdown();
		delete this->Worker;
	}

	TFuture<const RequestType*> Receive() override
	{
		int TempId = MsgID++;
		// New CallData goes on a pending queue see: https://github.com/grpc/grpc/blob/v1.47.4/src/core/lib/surface/server.cc#L413
		checkf(CurrExchange == nullptr, TEXT("Existing Exchange needs to be completed before a new exchange can be started"));
		CallData* CallDataPtr = new CallData(this->Service.get(), this->_CQueue.get(), this->TargetRPC);
		CurrExchange = CallDataPtr;
		CallDataPtr->Id = TempId;
		CallDataPtr->Create();
		UE_LOG(LogScholaCommunicator, VeryVerbose, TEXT("ExchangeRPcBackend %d: Started Exchange %d"), LocalID, CurrExchange->Id);

		return CurrExchange->GetRequestFuture();
	}

	void Respond(ResponseType* Response) override
	{
		assert(Service.Get() != nullptr);
		assert(CQueue.Get() != nullptr);
		checkf(CurrExchange != nullptr, TEXT("No Existing Exchange to Complete."));
		UE_LOG(LogScholaCommunicator, VeryVerbose, TEXT("ExchangeRPcBackend %d: Completed Exchange %d"), LocalID, CurrExchange->Id);
		CurrExchange->SetResponse(Response);
		CurrExchange->Submit();
		CurrExchange = nullptr;
	}

	virtual void Initialize() {};

	virtual void Start()
	{
		Worker->Start();
	}

	virtual void Establish() {};

	virtual void Shutdown() override
	{
		this->Worker->Stop();
		this->CurrExchange = nullptr;
	};

	virtual void Restart() {};

	virtual void Reset() override
	{
		// Clean up any stale exchange state from previous connection
		if (CurrExchange != nullptr)
		{
			UE_LOG(LogScholaCommunicator, Warning, TEXT("ExchangeRPCBackend %d: Completing stale exchange on reset"), LocalID);
			// Properly complete the pending exchange to avoid orphaning the CallData
			// This prevents the assertion failure when a new exchange is started
			Respond(new ResponseType());
		}
	}
};
