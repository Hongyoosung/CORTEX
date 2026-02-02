// ContextBridgeComponent.cpp
// Phase 3: StateTree context decoupling (dependency inversion for StateTree)

#include "StateTree/Components/ContextBridgeComponent.h"
#include "Team/ObjectiveActor.h"

UContextBridgeComponent::UContextBridgeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Initialize default tactical parameters
	TacticalParameters.Aggression = 0.5f;
	TacticalParameters.CoverPreference = 0.5f;
	TacticalParameters.SpreadDistance = 0.5f;
	TacticalParameters.RiskTolerance = 0.5f;

	// Initialize default combat parameters
	CombatParameters.Priority = ETargetPriority::Closest;
}

void UContextBridgeComponent::SetStrategy(EStrategyType InStrategy)
{
	if (CurrentStrategy != InStrategy)
	{
		CurrentStrategy = InStrategy;
		NotifyContextChanged();

		if (bEnableVerboseLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("[ContextBridge] %s: Strategy changed to %d"),
				*GetOwner()->GetName(),
				static_cast<int32>(InStrategy));
		}
	}
}

void UContextBridgeComponent::SetTargetObjective(AObjectiveActor* InObjective)
{
	if (TargetObjective != InObjective)
	{
		TargetObjective = InObjective;
		NotifyContextChanged();

		if (bEnableVerboseLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("[ContextBridge] %s: Target objective set to %s"),
				*GetOwner()->GetName(),
				InObjective ? *InObjective->GetName() : TEXT("None"));
		}
	}
}

void UContextBridgeComponent::SetTacticalParameters(const FTacticalParameters& InParams)
{
	TacticalParameters = InParams;
	NotifyContextChanged();

	if (bEnableVerboseLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("[ContextBridge] %s: Tactical parameters updated (Aggr:%.2f Cvr:%.2f)"),
			*GetOwner()->GetName(),
			InParams.Aggression,
			InParams.CoverPreference);
	}
}

void UContextBridgeComponent::SetCombatParameters(const FCombatParameters& InParams)
{
	CombatParameters = InParams;
	NotifyContextChanged();

	if (bEnableVerboseLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("[ContextBridge] %s: Combat parameters updated"),
			*GetOwner()->GetName());
	}
}

void UContextBridgeComponent::SetCurrentTarget(AActor* InTarget)
{
	if (CurrentTarget != InTarget)
	{
		CurrentTarget = InTarget;
		NotifyContextChanged();

		if (bEnableVerboseLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("[ContextBridge] %s: Target set to %s"),
				*GetOwner()->GetName(),
				InTarget ? *InTarget->GetName() : TEXT("None"));
		}
	}
}

void UContextBridgeComponent::SetAlertLevel(int32 InLevel)
{
	if (AlertLevel != InLevel)
	{
		AlertLevel = FMath::Clamp(InLevel, 0, 2);  // Clamp to valid range [0-2]
		NotifyContextChanged();

		if (bEnableVerboseLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("[ContextBridge] %s: Alert level set to %d"),
				*GetOwner()->GetName(),
				AlertLevel);
		}
	}
}

void UContextBridgeComponent::SetIsAlive(bool bInAlive)
{
	if (bIsAlive != bInAlive)
	{
		bIsAlive = bInAlive;
		NotifyContextChanged();

		if (bEnableVerboseLogging)
		{
			UE_LOG(LogTemp, Log, TEXT("[ContextBridge] %s: IsAlive set to %s"),
				*GetOwner()->GetName(),
				bInAlive ? TEXT("true") : TEXT("false"));
		}
	}
}

void UContextBridgeComponent::UpdateContext(
	EStrategyType InStrategy,
	AObjectiveActor* InObjective,
	const FTacticalParameters& InTacticalParams,
	const FCombatParameters& InCombatParams,
	AActor* InTarget,
	int32 InAlertLevel
)
{
	// Suppress notifications during bulk update
	bool bWasSuppressed = bSuppressNotifications;
	bSuppressNotifications = true;

	// Update all context fields
	CurrentStrategy = InStrategy;
	TargetObjective = InObjective;
	TacticalParameters = InTacticalParams;
	CombatParameters = InCombatParams;
	CurrentTarget = InTarget;
	AlertLevel = FMath::Clamp(InAlertLevel, 0, 2);

	// Restore notification state and fire single notification
	bSuppressNotifications = bWasSuppressed;
	NotifyContextChanged();

	if (bEnableVerboseLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("[ContextBridge] %s: Bulk context update completed"),
			*GetOwner()->GetName());
	}
}

void UContextBridgeComponent::ResetContext()
{
	if (bEnableVerboseLogging)
	{
		UE_LOG(LogTemp, Log, TEXT("[ContextBridge] %s: Resetting context to defaults"),
			*GetOwner()->GetName());
	}

	// Suppress notifications during reset
	bool bWasSuppressed = bSuppressNotifications;
	bSuppressNotifications = true;

	// Reset to defaults
	CurrentStrategy = EStrategyType::Assault;
	TargetObjective = nullptr;
	CurrentTarget = nullptr;
	AlertLevel = 0;
	bIsAlive = true;

	// Reset tactical parameters to neutral
	TacticalParameters.Aggression = 0.5f;
	TacticalParameters.CoverPreference = 0.5f;
	TacticalParameters.SpreadDistance = 0.5f;
	TacticalParameters.RiskTolerance = 0.5f;

	// Reset combat parameters
	CombatParameters.Priority = ETargetPriority::Closest;

	// Restore notification state and fire single notification
	bSuppressNotifications = bWasSuppressed;
	NotifyContextChanged();
}

void UContextBridgeComponent::NotifyContextChanged()
{
	if (!bSuppressNotifications)
	{
		OnContextUpdated.Broadcast();
	}
}
