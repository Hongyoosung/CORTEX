// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/BT/Decorators/BTDecorator_DEIsNotSupport.h"
#include "Characters/DEAgent.h"
#include "Types/DEStrategyTypes.h"
#include "AIController.h"

UBTDecorator_DEIsNotSupport::UBTDecorator_DEIsNotSupport()
{
	NodeName = TEXT("Is Not Support (Combat Unit)");
	// Allow the BT to abort this branch when strategy changes at runtime
	bNotifyBecomeRelevant = true;
	bNotifyCeaseRelevant  = false;
}

bool UBTDecorator_DEIsNotSupport::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	const AAIController* AICtrl = OwnerComp.GetAIOwner();
	if (!AICtrl) return false;

	const ADEAgent* Character = Cast<ADEAgent>(AICtrl->GetPawn());
	if (!Character) return false;

	return Character->GetCommandedStrategy() != EDEStrategyType::Support;
}

FString UBTDecorator_DEIsNotSupport::GetStaticDescription() const
{
	return TEXT("Strategy != Support\n(Assault or Defend — combat units only)");
}
