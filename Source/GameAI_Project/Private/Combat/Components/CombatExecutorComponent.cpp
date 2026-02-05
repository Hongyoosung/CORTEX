#include "Combat/Components/CombatExecutorComponent.h"
#include "RL/Components/RewardCalculator.h"
#include "Combat/Components/AgentPerceptionComponent.h"
#include "Combat/Components/HealthComponent.h"
#include "Combat/Components/WeaponComponent.h"
#include "AIController.h"
#include "Team/TeamTypes.h"

UCombatExecutorComponent::UCombatExecutorComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // Called explicitly from FollowerAgentComponent
}

void UCombatExecutorComponent::BeginPlay()
{
	Super::BeginPlay();

	// v9.0 Phase 3: Components are now injected by character (no FindComponentByClass)
	// Event binding happens in setter methods below

	UE_LOG(LogTemp, Log, TEXT("[CombatExecutor v9.0] '%s': Waiting for dependency injection"), *GetOwner()->GetName());
}

void UCombatExecutorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unbind events
	if (CachedHealthComponent)
	{
		CachedHealthComponent->OnDamageTaken.RemoveDynamic(this, &UCombatExecutorComponent::OnDamageTakenEvent);
		CachedHealthComponent->OnDamageDealt.RemoveDynamic(this, &UCombatExecutorComponent::OnDamageDealtEvent);
		CachedHealthComponent->OnKillConfirmed.RemoveDynamic(this, &UCombatExecutorComponent::OnKillEvent);
		CachedHealthComponent->OnDeath.RemoveDynamic(this, &UCombatExecutorComponent::OnDeathEvent);
	}

	Super::EndPlay(EndPlayReason);
}

//------------------------------------------------------------------------------
// v9.0 PHASE 3: DEPENDENCY INJECTION
//------------------------------------------------------------------------------

void UCombatExecutorComponent::SetHealthComponent(UHealthComponent* Health)
{
	CachedHealthComponent = Health;

	if (CachedHealthComponent)
	{
		// Bind to HealthComponent events for RL reward integration
		CachedHealthComponent->OnDamageTaken.AddDynamic(this, &UCombatExecutorComponent::OnDamageTakenEvent);
		CachedHealthComponent->OnDamageDealt.AddDynamic(this, &UCombatExecutorComponent::OnDamageDealtEvent);
		CachedHealthComponent->OnKillConfirmed.AddDynamic(this, &UCombatExecutorComponent::OnKillEvent);
		CachedHealthComponent->OnDeath.AddDynamic(this, &UCombatExecutorComponent::OnDeathEvent);

		UE_LOG(LogTemp, Log, TEXT("[CombatExecutor v9.0] '%s': Injected HealthComponent and bound events"),
			*GetOwner()->GetName());
	}
}

void UCombatExecutorComponent::SetPerceptionComponent(UAgentPerceptionComponent* Perception)
{
	CachedPerceptionComponent = Perception;

	if (CachedPerceptionComponent)
	{
		UE_LOG(LogTemp, Log, TEXT("[CombatExecutor v9.0] '%s': Injected PerceptionComponent"),
			*GetOwner()->GetName());
	}
}

//------------------------------------------------------------------------------
// COMBAT EXECUTION (v8.0)
//------------------------------------------------------------------------------

void UCombatExecutorComponent::ExecuteCombat(const FCombatParameters& CombatParams)
{
	// ========================================
	// v8.0 COMBAT SYSTEM (Layer 4 of Hierarchy)
	//
	// ARCHITECTURE:
	// - RL selects target priority (Closest vs LowestHP)
	// - Auto-aim handles targeting (no learned aiming)
	// - Auto-fire when has LOS
	//
	// DESIGN DECISION:
	// v8.0 focuses on tactical positioning + target selection
	// Learned aiming deferred to v8.5 (reduces complexity)
	// ========================================

	if (!GetOwner() || !bIsAlive)
	{
		return;
	}

	// Get weapon component
	UWeaponComponent* WeaponComp = GetOwner()->FindComponentByClass<UWeaponComponent>();
	if (!WeaponComp)
	{
		return;
	}

	// Get detected enemies from perception
	if (!CachedPerceptionComponent)
	{
		return;
	}


	// ========================================
	// v8.0: RL-controlled target selection
	// Combat parameters determine priority
	// ========================================

	AActor* Target = nullptr;


	if (!Target)
	{
		return;
	}

	// ========================================
	// Auto-aim and auto-fire (v8.0)
	// No learned aiming - engine handles targeting
	// ========================================

	AAIController* AIController = Cast<AAIController>(GetOwner()->GetInstigatorController());
	if (AIController)
	{
		// Set focus for auto-aim
		AIController->SetFocus(Target, EAIFocusPriority::Gameplay);
	}

	// ========================================
	// Facing check before firing
	// Prevents agents from firing while moving backwards
	// ========================================
	FVector OwnerLocation = GetOwner()->GetActorLocation();
	FVector TargetLocation = Target->GetActorLocation();
	FVector DirectionToTarget = (TargetLocation - OwnerLocation).GetSafeNormal();
	FVector AgentForward = GetOwner()->GetActorForwardVector();

	// Calculate angle between forward vector and target direction
	float DotProduct = FVector::DotProduct(AgentForward, DirectionToTarget);
	float AngleToTarget = FMath::Acos(DotProduct) * (180.0f / PI);

	// Only proceed if agent is facing target within tolerance
	if (AngleToTarget > MaxFacingAngleToFire)
	{
		// Agent not facing target yet - SetFocus will rotate, but don't fire yet
		UE_LOG(LogTemp, VeryVerbose, TEXT("[COMBAT v8.0] '%s': Not facing target '%s' (%.1f° > %.1f°), holding fire"),
			*GetOwner()->GetName(),
			*Target->GetName(),
			AngleToTarget,
			MaxFacingAngleToFire);
		return;
	}

	// ========================================
	// Line-of-sight check before firing
	// ========================================

	// Eye-level adjustment for accurate LOS check
	FVector StartLocation = OwnerLocation + FVector(0, 0, 150.0f);
	FVector EndLocation = TargetLocation + FVector(0, 0, 150.0f);

	FHitResult HitResult;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(GetOwner());
	Params.bTraceComplex = false;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	bool bHitSomething = World->LineTraceSingleByChannel(
		HitResult,
		StartLocation,
		EndLocation,
		ECC_Visibility,
		Params
	);

	// Only fire if we have clear LOS to target (not blocked by walls/obstacles)
	if (bHitSomething && HitResult.GetActor() != Target)
	{
		// LOS blocked - don't waste ammo on walls
		UE_LOG(LogTemp, VeryVerbose, TEXT("[COMBAT v8.0] '%s': Target '%s' blocked by '%s', holding fire"),
			*GetOwner()->GetName(),
			*Target->GetName(),
			*GetNameSafe(HitResult.GetActor()));
		return;
	}

	// ========================================
	// Fire weapon if able
	// ========================================
	if (WeaponComp->CanFire())
	{
		// Calculate fire direction
		FVector FireDirection = DirectionToTarget;

		// Fire in direction (no auto-aim assistance for RL agents)
		bool bFired = WeaponComp->FireInDirection(FireDirection);

		if (bFired)
		{
			UE_LOG(LogTemp, VeryVerbose, TEXT("[COMBAT v8.0] '%s' → '%s': Fired (Priority: %s, angle: %.1f°, clear LOS)"),
				*GetOwner()->GetName(),
				*Target->GetName(),
				*UEnum::GetValueAsString(CombatParams.Priority),
				AngleToTarget);
		}
	}
}

AActor* UCombatExecutorComponent::GetClosestEnemy(const TArray<AActor*>& Enemies) const
{
	if (Enemies.Num() == 0 || !GetOwner())
	{
		return nullptr;
	}

	FVector AgentLocation = GetOwner()->GetActorLocation();
	AActor* ClosestEnemy = nullptr;
	float ClosestDistance = MAX_FLT;

	for (AActor* Enemy : Enemies)
	{
		if (!Enemy)
		{
			continue;
		}

		float Distance = FVector::Dist(AgentLocation, Enemy->GetActorLocation());
		if (Distance < ClosestDistance)
		{
			ClosestDistance = Distance;
			ClosestEnemy = Enemy;
		}
	}

	return ClosestEnemy;
}

AActor* UCombatExecutorComponent::GetLowestHPEnemy(const TArray<AActor*>& Enemies) const
{
	if (Enemies.Num() == 0)
	{
		return nullptr;
	}

	AActor* LowestHPEnemy = nullptr;
	float LowestHP = MAX_FLT;

	for (AActor* Enemy : Enemies)
	{
		if (!Enemy)
		{
			continue;
		}

		// Get enemy health component
		UHealthComponent* EnemyHealthComp = Enemy->FindComponentByClass<UHealthComponent>();
		if (!EnemyHealthComp || !EnemyHealthComp->IsAlive())
		{
			continue;
		}

		float EnemyHP = EnemyHealthComp->GetCurrentHealth();
		if (EnemyHP < LowestHP)
		{
			LowestHP = EnemyHP;
			LowestHPEnemy = Enemy;
		}
	}

	// Fallback: If no valid health components found, return closest enemy
	if (!LowestHPEnemy)
	{
		return GetClosestEnemy(Enemies);
	}

	return LowestHPEnemy;
}

//------------------------------------------------------------------------------
// COMBAT EVENT HANDLERS (RL REWARD INTEGRATION v5.0)
//------------------------------------------------------------------------------

void UCombatExecutorComponent::OnDamageTakenEvent(const FDamageEventData& DamageEvent, float CurrentHealth)
{
	// Notify RewardCalculator (v5.0 strategy-specific rewards)
	if (RewardCalculator)
	{
		RewardCalculator->OnTakeDamage(DamageEvent.DamageAmount);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[CombatExecutor] '%s': RewardCalculator missing! Cannot calculate damage reward."),
			*GetOwner()->GetName());
	}

	UE_LOG(LogTemp, Verbose, TEXT("[CombatExecutor] '%s': Took %.1f damage from %s"),
		*GetOwner()->GetName(),
		DamageEvent.DamageAmount,
		DamageEvent.Instigator ? *DamageEvent.Instigator->GetName() : TEXT("Unknown"));
}

void UCombatExecutorComponent::OnDamageDealtEvent(AActor* Victim, float DamageAmount)
{
	// Notify RewardCalculator (v5.0 strategy-specific rewards)
	if (RewardCalculator)
	{
		RewardCalculator->OnDealDamage(DamageAmount, Victim);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[CombatExecutor] '%s': RewardCalculator missing! Cannot calculate damage reward."),
			*GetOwner()->GetName());
	}

	UE_LOG(LogTemp, Verbose, TEXT("[CombatExecutor] '%s': Dealt %.1f damage to %s"),
		*GetOwner()->GetName(),
		DamageAmount,
		Victim ? *Victim->GetName() : TEXT("Unknown"));
}

void UCombatExecutorComponent::OnKillEvent(AActor* Victim, float TotalDamage)
{
	// Notify RewardCalculator (v5.0 strategy-specific rewards)
	if (RewardCalculator)
	{
		RewardCalculator->OnKillEnemy(Victim);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[CombatExecutor] '%s': RewardCalculator missing! Cannot calculate kill reward."),
			*GetOwner()->GetName());
	}

	UE_LOG(LogTemp, Log, TEXT("[CombatExecutor] '%s': KILL confirmed on %s"),
		*GetOwner()->GetName(),
		Victim ? *Victim->GetName() : TEXT("Unknown"));
}

void UCombatExecutorComponent::OnDeathEvent(const FDeathEventData& DeathEvent)
{
	// Notify RewardCalculator (v5.0 strategy-specific rewards)
	if (RewardCalculator)
	{
		RewardCalculator->OnDeath();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[CombatExecutor] '%s': RewardCalculator missing! Cannot calculate death penalty."),
			*GetOwner()->GetName());
	}

	UE_LOG(LogTemp, Warning, TEXT("[CombatExecutor] '%s': DIED (Killed by %s)"),
		*GetOwner()->GetName(),
		DeathEvent.Killer ? *DeathEvent.Killer->GetName() : TEXT("Unknown"));

	// Mark as dead
	bIsAlive = false;
}
