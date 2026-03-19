// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/BT/Decorators/BTDecorator_DEShouldHeal.h"
#include "AIController.h"
#include "Characters/DECharacter.h"
#include "GAS/Abilities/DEGA_Heal.h"

UBTDecorator_DEShouldHeal::UBTDecorator_DEShouldHeal()
{
	NodeName = TEXT("Should Heal");
	// Re-evaluate every time the agent takes damage (no BB key needed — tick-based)
	bNotifyBecomeRelevant = false;
	bNotifyCeaseRelevant  = false;
}

bool UBTDecorator_DEShouldHeal::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	const AAIController* AIC = Cast<AAIController>(OwnerComp.GetAIOwner());
	const ADECharacter* Char = AIC ? Cast<ADECharacter>(AIC->GetPawn()) : nullptr;
	if (!Char || !Char->IsAlive()) return false;

	if (Char->GetCommandedStrategy() != EDEStrategyType::Support) return false;

	// Trigger whenever there is an injured ally in heal range.
	// HealthThreshold is forwarded so the ability uses the same definition of "injured"
	// as the decorator (default 1.0 = heal anyone not at full health).
	UDEGA_Heal* HealAbility = Char->GetHealAbility();
	return HealAbility && HealAbility->HasInjuredAllyInRange(HealthThreshold);
}

FString UBTDecorator_DEShouldHeal::GetStaticDescription() const
{
	return TEXT("Support only | Has injured ally in heal range");
}
