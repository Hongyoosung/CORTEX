// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/MocAbilityBase.h"
#include "Characters/MocCharacter.h"

UMocAbilityBase::UMocAbilityBase()
{
	PrimaryComponentTick.bCanEverTick = false;
}

AMocCharacter* UMocAbilityBase::GetOwnerCharacter() const
{
	return Cast<AMocCharacter>(GetOwner());
}
