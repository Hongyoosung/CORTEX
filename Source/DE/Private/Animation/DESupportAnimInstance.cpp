// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/DESupportAnimInstance.h"
#include "Animation/AnimMontage.h"

const FName UDESupportAnimInstance::SectionStart(TEXT("Start"));
const FName UDESupportAnimInstance::SectionLoop (TEXT("Loop"));
const FName UDESupportAnimInstance::SectionEnd  (TEXT("End"));

// ---------------------------------------------------------------------------

void UDESupportAnimInstance::BeginHealLoop(UAnimMontage* InMontage)
{
	ActiveHealMontage = InMontage;
	bIsHealChanneling = true;
}

// ---------------------------------------------------------------------------

void UDESupportAnimInstance::StopHealLoop()
{
	// Section redirect (Loop→End) is applied by DEGA_Heal on the base AnimInstance
	// in EndAbility, so only state needs to be cleared here.
	bIsHealChanneling = false;
	ActiveHealMontage = nullptr;
}

// ---------------------------------------------------------------------------
// Fired by DEAnimNotify_HealLoopStart at the tail of the "Start" section.
// Hook for Blueprint state machines or visual effects that need to react
// to the moment the loop begins.
// ---------------------------------------------------------------------------

void UDESupportAnimInstance::OnHealLoopStartNotify()
{
	// No-op — override in Blueprint to drive blend-tree transitions or trigger VFX.
}
