// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/DEAnimNotify_HealLoopStart.h"
#include "Animation/DESupportAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

void UDEAnimNotify_HealLoopStart::Notify(USkeletalMeshComponent* MeshComp,
                                          UAnimSequenceBase* Animation,
                                          const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp) return;

	UAnimInstance* AnimInstance = MeshComp->GetAnimInstance();
	if (!AnimInstance) return;

	// If this notify lives on an AnimSequence asset (not directly on the montage
	// timeline), Animation is a UAnimSequence and Cast<UAnimMontage> returns null.
	// Use GetCurrentActiveMontage() instead — it always returns the live instance.
	UAnimMontage* Montage = Cast<UAnimMontage>(Animation);
	if (!Montage)
	{
		Montage = AnimInstance->GetCurrentActiveMontage();
	}

	// Re-apply Loop→Loop here rather than at Montage_Play time.
	// By the time this notify fires the AnimBlueprint has had at least one full
	// tick to react to bIsHealChanneling, so any ABP-driven Montage_Play call
	// has already created its fresh FAnimMontageInstance.  Writing the redirect
	// now targets the correct, final instance.
	if (Montage)
	{
		AnimInstance->Montage_SetNextSection(
			UDESupportAnimInstance::SectionLoop,
			UDESupportAnimInstance::SectionLoop,
			Montage);
	}

	// Notify the support AnimInstance for state / Blueprint hooks,
	// and let it cache the live montage pointer for use in StopHealLoop.
	if (UDESupportAnimInstance* SupportAnim = Cast<UDESupportAnimInstance>(AnimInstance))
	{
		SupportAnim->CacheActiveMontage(Montage);
		SupportAnim->OnHealLoopStartNotify();
	}
}
