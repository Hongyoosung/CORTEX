// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BTDecorator_DEShouldHeal.generated.h"

/**
 * BTDecorator_DEShouldHeal
 *
 * Returns true when the owning agent's health is not full (Health < MaxHealth).
 * Attach to a Heal branch in the BT with Observer Aborts = Lower Priority so
 * the agent interrupts movement and heals whenever it takes damage.
 */
UCLASS(Blueprintable)
class DE_API UBTDecorator_DEShouldHeal : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_DEShouldHeal();

protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual FString GetStaticDescription() const override;

public:
	/** Health percentage threshold below which healing is triggered (0.0–1.0).
	 *  Default 1.0 = heal whenever not at full health. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heal", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HealthThreshold = 1.0f;
};
