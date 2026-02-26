// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MocAbilityBase.generated.h"

class AMocCharacter;

/**
 * UMocAbilityBase - Base class for ability components attached to AMocCharacter.
 *
 * Supports two execution modes:
 * - Training mode: ExecuteAbility() self-selects target and acts
 * - Inference mode: ExecuteAbilityWithTarget() receives explicit target from BT task
 */
UCLASS(Abstract, ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class GAMEAI_PROJECT_API UMocAbilityBase : public UActorComponent
{
	GENERATED_BODY()

public:
	UMocAbilityBase();

	/** Training mode: self-select target and act */
	virtual void ExecuteAbility(float DeltaTime) {}

	/** Inference mode: BT task passes explicit target */
	virtual void ExecuteAbilityWithTarget(float DeltaTime, AActor* Target) {}

protected:
	/** Cast GetOwner() to AMocCharacter */
	AMocCharacter* GetOwnerCharacter() const;
};
