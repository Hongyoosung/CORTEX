#pragma once

#include "CoreMinimal.h"
#include "TeamTypes.generated.h"


UENUM(BlueprintType)
enum class ETeamEpisodeResult : uint8
{
	Win     UMETA(DisplayName = "Win"),
	Loss    UMETA(DisplayName = "Loss"),
	Draw    UMETA(DisplayName = "Draw / Timeout")
};

/**
 * Strategic events that trigger MCTS decision-making
 */
UENUM(BlueprintType)
enum class EStrategicEvent : uint8
{
	// Combat events
	AllyKilled          UMETA(DisplayName = "Ally Killed"),
	EnemyKilled			UMETA(DisplayName = "Enemy Killed"),

	// Custom
	Custom              UMETA(DisplayName = "Custom Event")
};


/**
 * Event context information
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FTeamInfo
{
	GENERATED_BODY()

	/** Team ID for this leader */
	UPROPERTY(EditAnywhere, Category = "AI|Team", meta = (AllowPrivateAccess = "true"))
	int32 TeamID;

	UPROPERTY(EditAnywhere, Category = "AI|Team", meta = (AllowPrivateAccess = "true"))
	int32 EnvID;

	int32 AgnetCount;

	FLinearColor TeamColor;

	FTeamInfo()
		: TeamID(0)
		, EnvID(0)
		, AgnetCount(0)
		, TeamColor(FLinearColor::White)
	{}
};