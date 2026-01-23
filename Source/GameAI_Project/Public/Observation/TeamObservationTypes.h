#pragma once

#include "CoreMinimal.h"
#include "TeamObservationTypes.generated.h"

/**
 * Engagement range classification
 */
UENUM(BlueprintType)
enum class EEngagementRange : uint8
{
    VeryClose   UMETA(DisplayName = "Very Close (< 5m)"),
    Close       UMETA(DisplayName = "Close (5-15m)"),
    Medium      UMETA(DisplayName = "Medium (15-30m)"),
    Long        UMETA(DisplayName = "Long (30-50m)"),
    VeryLong    UMETA(DisplayName = "Very Long (> 50m)")
};


// v8.0: EMissionPhase removed - no longer used in strategy-based architecture
