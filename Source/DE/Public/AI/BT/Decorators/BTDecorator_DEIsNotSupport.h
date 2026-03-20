// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BTDecorator_DEIsNotSupport.generated.h"

/**
 * BTDecorator_DEIsNotSupport
 *
 * Returns true when the owning agent's commanded strategy is Assault or Defend
 * (i.e., a combat unit). Returns false for Support agents.
 *
 * Attach to the attack branch in the Behavior Tree so that Support agents
 * are blocked from using offensive abilities — they should only heal.
 *
 * Observer Aborts: set to "Self" to re-evaluate immediately when strategy changes.
 */
UCLASS(Blueprintable)
class DE_API UBTDecorator_DEIsNotSupport : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_DEIsNotSupport();

protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual FString GetStaticDescription() const override;
};
