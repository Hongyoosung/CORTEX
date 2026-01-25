#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RL/RLTypes.h"
#include "CombatExecutorComponent.generated.h"

// Forward declarations
class URewardCalculator;
class UAgentPerceptionComponent;
class UHealthComponent;
class UTeamLeaderComponent;
struct FDamageEventData;
struct FDeathEventData;

/**
 * Combat Executor Component
 *
 * Responsibilities (v8.0):
 * - Execute combat actions using learned target priority
 * - Target selection (Closest, LowestHP)
 * - Combat event handling (damage, kill, death)
 * - Auto-aim and auto-fire
 *
 * This component was extracted from FollowerAgentComponent to improve separation of concerns.
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UCombatExecutorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatExecutorComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	//--------------------------------------------------------------------------
	// COMBAT EXECUTION (v8.0)
	//--------------------------------------------------------------------------

	/** Execute combat actions using learned target priority and auto-aim */
	UFUNCTION(BlueprintCallable, Category = "Combat|Execution")
	void ExecuteCombat(const FCombatParameters& CombatParams);

	/** Get closest enemy from perception */
	UFUNCTION(BlueprintPure, Category = "Combat|Targeting")
	AActor* GetClosestEnemy(const TArray<AActor*>& Enemies) const;

	/** Get enemy with lowest HP from perception */
	UFUNCTION(BlueprintPure, Category = "Combat|Targeting")
	AActor* GetLowestHPEnemy(const TArray<AActor*>& Enemies) const;

public:
	//--------------------------------------------------------------------------
	// CONFIGURATION
	//--------------------------------------------------------------------------

	/** Team leader for event signaling (optional, can be set externally) */
	UPROPERTY(BlueprintReadWrite, Category = "Combat|Config")
	TObjectPtr<UTeamLeaderComponent> TeamLeader = nullptr;

	/** Reward calculator for combat rewards (optional, can be set externally) */
	UPROPERTY(BlueprintReadWrite, Category = "Combat|Config")
	TObjectPtr<URewardCalculator> RewardCalculator = nullptr;

	/** Is agent alive? (required for combat execution) */
	UPROPERTY(BlueprintReadWrite, Category = "Combat|Config")
	bool bIsAlive = true;

	/** Maximum angle (degrees) between forward vector and target for firing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Config")
	float MaxFacingAngleToFire = 15.0f;

private:
	//--------------------------------------------------------------------------
	// COMBAT EVENT HANDLERS
	//--------------------------------------------------------------------------

	UFUNCTION()
	void OnDamageTakenEvent(const FDamageEventData& DamageEvent, float CurrentHealth);

	UFUNCTION()
	void OnDamageDealtEvent(AActor* Victim, float DamageAmount);

	UFUNCTION()
	void OnKillEvent(AActor* Victim, float TotalDamage);

	UFUNCTION()
	void OnDeathEvent(const FDeathEventData& DeathEvent);

	//--------------------------------------------------------------------------
	// CACHED COMPONENTS
	//--------------------------------------------------------------------------

	/** Cached health component */
	UPROPERTY()
	UHealthComponent* CachedHealthComponent = nullptr;

	/** Cached perception component */
	UPROPERTY()
	UAgentPerceptionComponent* CachedPerceptionComponent = nullptr;
};
