#include "RL/Components/TacticalStateComponent.h"
#include "Team/ObjectiveActor.h"

UTacticalStateComponent::UTacticalStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // Stateless component, no tick needed
}

void UTacticalStateComponent::BeginPlay()
{
	Super::BeginPlay();

	// v9.0: Initialize with default strategy assignment (no objective)
	CurrentAssignment.Agent = GetOwner();
	CurrentAssignment.Strategy = EStrategyType::Assault;
	// v9.0: TargetObjective removed - objectives implicit in strategy rewards
	CurrentAssignment.Priority = 5;
	CurrentAssignment.ExpectedValue = 0.0f;
	CurrentAssignment.VisitCount = 0;
	CurrentAssignment.Timestamp = GetWorld()->GetTimeSeconds();

	UE_LOG(LogTemp, Log, TEXT("TacticalStateComponent: Initialized on %s (Default Strategy: Assault)"),
		*GetOwner()->GetName());
}

//------------------------------------------------------------------------------
// v8.0: STRATEGY ASSIGNMENT
//------------------------------------------------------------------------------

void UTacticalStateComponent::SetStrategyAssignment(const FStrategyAssignment& Assignment)
{
	// Store previous assignment for change detection
	LastAssignment = CurrentAssignment;

	// Set new assignment
	CurrentAssignment = Assignment;

	// Update timestamp
	CurrentAssignment.Timestamp = GetWorld()->GetTimeSeconds();

	UE_LOG(LogTemp, Verbose, TEXT("[TacticalState] '%s': Strategy assignment updated - Strategy=%s"),
		*GetOwner()->GetName(),
		*UEnum::GetValueAsString(Assignment.Strategy));

	// v8.0 DEPRECATED: Update CurrentStrategy for backwards compatibility
	CurrentStrategy = Assignment.Strategy;
}

//------------------------------------------------------------------------------
// v8.0: TACTICAL & COMBAT PARAMETERS
//------------------------------------------------------------------------------

void UTacticalStateComponent::SetTacticalParameters(const FTacticalParameters& Params)
{
	CurrentMacroAction.TacticalParams = Params;

	UE_LOG(LogTemp, VeryVerbose, TEXT("[TacticalState] '%s': Tactical params set [Agg=%.2f, Cover=%.2f, Spread=%.2f, Risk=%.2f]"),
		*GetOwner()->GetName(),
		Params.Aggression, Params.CoverPreference, Params.SpreadDistance, Params.RiskTolerance);
}

void UTacticalStateComponent::SetCombatParameters(const FCombatParameters& Params)
{
	CurrentMacroAction.CombatParams = Params;

	UE_LOG(LogTemp, VeryVerbose, TEXT("[TacticalState] '%s': Combat params set [Priority=%s]"),
		*GetOwner()->GetName(),
		Params.Priority == ETargetPriority::Closest ? TEXT("Closest") : TEXT("LowestHP"));
}

//------------------------------------------------------------------------------
// STATE MANAGEMENT
//------------------------------------------------------------------------------

void UTacticalStateComponent::MarkAsDead()
{
	if (!bIsAlive) return;

	bIsAlive = false;

	UE_LOG(LogTemp, Warning, TEXT("[TacticalState] '%s': Marked as dead"), *GetOwner()->GetName());
}

void UTacticalStateComponent::MarkAsAlive()
{
	if (bIsAlive) return;

	bIsAlive = true;

	UE_LOG(LogTemp, Warning, TEXT("[TacticalState] '%s': Marked as alive"), *GetOwner()->GetName());
}

void UTacticalStateComponent::ResetState()
{
	// v9.0: Reset assignment to default (no objective)
	CurrentAssignment.Strategy = EStrategyType::Assault;
	// v9.0: TargetObjective removed - objectives implicit in strategy rewards
	CurrentAssignment.Priority = 5;
	LastAssignment = FStrategyAssignment();

	// Reset parameters
	CurrentMacroAction = FMacroAction();

	// v8.0 DEPRECATED: Reset CurrentStrategy for backwards compatibility
	CurrentStrategy = EStrategyType::Assault;

	UE_LOG(LogTemp, Log, TEXT("[TacticalState] '%s': State reset"), *GetOwner()->GetName());
}
