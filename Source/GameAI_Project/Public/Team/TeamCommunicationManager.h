#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TeamTypes.h"
#include "TeamCommunicationManager.generated.h"

// Forward declarations
class UTeamLeaderComponent;
class UFollowerAgentComponent;

/**
 * Message types for team communication
 */
UENUM(BlueprintType)
enum class ETeamMessageType : uint8
{
	// Leader → Follower
	FormationUpdate     UMETA(DisplayName = "Formation Update"),
	Acknowledgement     UMETA(DisplayName = "Acknowledgement"),

	// Follower → Leader
	EventSignal         UMETA(DisplayName = "Event Signal"),
	StatusReport        UMETA(DisplayName = "Status Report"),
	RequestAssistance   UMETA(DisplayName = "Request Assistance"),

	// Follower ↔ Follower (optional)
	PeerMessage         UMETA(DisplayName = "Peer Message"),
	BroadcastMessage    UMETA(DisplayName = "Broadcast Message")
};

/**
 * Team message structure
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FTeamMessage
{
	GENERATED_BODY()

	/** Message type */
	UPROPERTY(BlueprintReadWrite, Category = "Message")
	ETeamMessageType MessageType = ETeamMessageType::EventSignal;

	/** Sender actor */
	UPROPERTY(BlueprintReadWrite, Category = "Message")
	AActor* Sender = nullptr;

	/** Recipient actor */
	UPROPERTY(BlueprintReadWrite, Category = "Message")
	AActor* Recipient = nullptr;

	/** Message priority (0-10) */
	UPROPERTY(BlueprintReadWrite, Category = "Message")
	int32 Priority = 5;

	/** Timestamp when message was sent */
	UPROPERTY(BlueprintReadOnly, Category = "Message")
	float Timestamp = 0.0f;

	/** Event context (if applicable) */
	UPROPERTY(BlueprintReadWrite, Category = "Message")
	FStrategicEventContext EventContext;

	/** Additional message data (key-value pairs) */
	UPROPERTY(BlueprintReadWrite, Category = "Message")
	TMap<FString, FString> MessageData;

	FTeamMessage()
	{
		Timestamp = FPlatformTime::Seconds();
	}
};

/**
 * Delegate for message events
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnMessageSent,
	const FTeamMessage&, Message
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnMessageReceived,
	const FTeamMessage&, Message
);

/**
 * Team Communication Manager (v3.0)
 *
 * Responsibilities:
 * - Manage message passing between team leader and followers
 * - Route messages to appropriate recipients
 * - Maintain message queue with priority ordering
 * - Provide peer-to-peer messaging (optional)
 * - Track communication metrics
 *
 * Usage:
 * 1. Create one manager per team (or share across teams)
 * 2. FollowerAgentComponent uses SendEventToLeader()
 * 3. Messages are delivered immediately or queued based on priority
 *
 * Note: v3.0 uses MissionManager for Mission assignment (not CommunicationManager)
 */
UCLASS(BlueprintType)
class GAMEAI_PROJECT_API UTeamCommunicationManager : public UObject
{
	GENERATED_BODY()

public:
	UTeamCommunicationManager();


	//--------------------------------------------------------------------------
	// LEADER → FOLLOWER MESSAGING
	//--------------------------------------------------------------------------

	/** Send formation update to follower */
	UFUNCTION(BlueprintCallable, Category = "Communication|Leader")
	void SendFormationUpdate(
		UTeamLeaderComponent* Leader,
		AActor* Follower,
		FVector FormationPosition,
		FRotator FormationRotation
	);

	/** Send acknowledgement to follower */
	UFUNCTION(BlueprintCallable, Category = "Communication|Leader")
	void SendAcknowledgement(
		UTeamLeaderComponent* Leader,
		AActor* Follower,
		const FString& AcknowledgementMessage
	);

	//--------------------------------------------------------------------------
	// FOLLOWER → LEADER MESSAGING
	//--------------------------------------------------------------------------

	/** Send event from follower to leader */
	UFUNCTION(BlueprintCallable, Category = "Communication|Follower")
	void SendEventToLeader(
		UFollowerAgentComponent* Follower,
		UTeamLeaderComponent* Leader,
		const FStrategicEventContext& EventContext
	);

	/** Send status report from follower to leader */
	UFUNCTION(BlueprintCallable, Category = "Communication|Follower")
	void SendStatusReport(
		UFollowerAgentComponent* Follower,
		UTeamLeaderComponent* Leader,
		float Health,
		float Ammo,
		const FString& StatusMessage
	);

	/** Request assistance from team */
	UFUNCTION(BlueprintCallable, Category = "Communication|Follower")
	void SendAssistanceRequest(
		UFollowerAgentComponent* Follower,
		UTeamLeaderComponent* Leader,
		int32 Priority
	);

	//--------------------------------------------------------------------------
	// PEER-TO-PEER MESSAGING (OPTIONAL)
	//--------------------------------------------------------------------------

	/** Send message from one follower to another */
	UFUNCTION(BlueprintCallable, Category = "Communication|Peer")
	void SendPeerMessage(
		AActor* Sender,
		AActor* Recipient,
		const FString& Message,
		int32 Priority = 5
	);

	/** Broadcast message to nearby followers */
	UFUNCTION(BlueprintCallable, Category = "Communication|Peer")
	void BroadcastToNearby(
		AActor* Sender,
		float Radius,
		const FString& Message,
		int32 Priority = 5
	);

	//--------------------------------------------------------------------------
	// MESSAGE QUEUE MANAGEMENT
	//--------------------------------------------------------------------------

	/** Add message to queue */
	UFUNCTION(BlueprintCallable, Category = "Communication|Queue")
	void QueueMessage(const FTeamMessage& Message);

	/** Process message queue (call in Tick) */
	UFUNCTION(BlueprintCallable, Category = "Communication|Queue")
	void ProcessMessageQueue();

	/** Clear message queue */
	UFUNCTION(BlueprintCallable, Category = "Communication|Queue")
	void ClearMessageQueue();

	/** Get queued message count */
	UFUNCTION(BlueprintPure, Category = "Communication|Queue")
	int32 GetQueuedMessageCount() const { return MessageQueue.Num(); }

	//--------------------------------------------------------------------------
	// UTILITY
	//--------------------------------------------------------------------------

	/** Get communication statistics */
	UFUNCTION(BlueprintPure, Category = "Communication|Stats")
	void GetCommunicationStats(int32& OutSent, int32& OutReceived, int32& OutQueued) const
	{
		OutSent = TotalMessagesSent;
		OutReceived = TotalMessagesReceived;
		OutQueued = MessageQueue.Num();
	}

	/** Reset statistics */
	UFUNCTION(BlueprintCallable, Category = "Communication|Stats")
	void ResetStatistics()
	{
		TotalMessagesSent = 0;
		TotalMessagesReceived = 0;
	}


private:
	/** Deliver message immediately */
	void DeliverMessage(const FTeamMessage& Message);

	/** Log message (if logging enabled) */
	void LogMessage(const FTeamMessage& Message, bool bSending);


public:
	//--------------------------------------------------------------------------
	// CONFIGURATION
	//--------------------------------------------------------------------------

	/** Enable message queueing (if false, deliver immediately) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Communication|Config")
	bool bEnableMessageQueue = false;

	/** Maximum messages in queue */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Communication|Config")
	int32 MaxQueueSize = 100;

	/** Enable peer-to-peer messaging */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Communication|Config")
	bool bEnablePeerToPeer = true;

	/** Enable message logging */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Communication|Config")
	bool bEnableMessageLogging = true;

	//--------------------------------------------------------------------------
	// STATE
	//--------------------------------------------------------------------------

	/** Message queue (priority-ordered) */
	UPROPERTY(BlueprintReadOnly, Category = "Communication|State")
	TArray<FTeamMessage> MessageQueue;

	/** Total messages sent */
	UPROPERTY(BlueprintReadOnly, Category = "Communication|Stats")
	int32 TotalMessagesSent = 0;

	/** Total messages received */
	UPROPERTY(BlueprintReadOnly, Category = "Communication|Stats")
	int32 TotalMessagesReceived = 0;

	//--------------------------------------------------------------------------
	// EVENTS
	//--------------------------------------------------------------------------

	/** Fired when message is sent */
	UPROPERTY(BlueprintAssignable, Category = "Communication|Events")
	FOnMessageSent OnMessageSent;

	/** Fired when message is received */
	UPROPERTY(BlueprintAssignable, Category = "Communication|Events")
	FOnMessageReceived OnMessageReceived;
};
