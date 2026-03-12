// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/BT/Decorators/BTDecorator_DEShouldHeal.h"
#include "AIController.h"
#include "Characters/DECharacter.h"

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

	return Char->GetCommandedStrategy() == EDEStrategyType::Support
		&& Char->GetHealthPercentage() < HealthThreshold;
}

FString UBTDecorator_DEShouldHeal::GetStaticDescription() const
{
	return FString::Printf(TEXT("Support only | Health < %.0f%%"), HealthThreshold * 100.0f);
}
