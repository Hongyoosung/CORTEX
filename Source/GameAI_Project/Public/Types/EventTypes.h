// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EventTypes.generated.h"

/**
 * MOC v10.2: Critical event types that trigger immediate replanning
 */
UENUM(BlueprintType)
enum class ECriticalEventType : uint8
{
	AllyKilled UMETA(DisplayName = "Ally Killed"),
	EnemyKilled UMETA(DisplayName = "Enemy Killed"),
	ObjectiveCaptured UMETA(DisplayName = "Objective Captured"),
	ObjectiveLost UMETA(DisplayName = "Objective Lost"),
	HealthCritical UMETA(DisplayName = "Team Health Critical"), // Team average < 30%
	COUNT UMETA(Hidden)
};
