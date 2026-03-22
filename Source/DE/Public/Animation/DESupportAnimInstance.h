// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "DESupportAnimInstance.generated.h"

class UAnimMontage;

/**
 * UDESupportAnimInstance
 *
 * AnimInstance for the Support-role agent.
 * Drives the heal-channel montage loop:
 *
 *   Montage sections expected:  "Start" → "Loop" → "End"
 *
 *   Flow:
 *   1. DEGA_Heal calls Montage_Play(HealMontage) then BeginHealLoop().
 *   2. The "Start" section plays once.
 *   3. DEGA_Heal re-asserts "Loop"→"Loop" every frame while channeling,
 *      keeping the montage frozen in the Loop section.
 *   4. When DEGA_Heal::EndAbility calls StopHealLoop(), the next redirect
 *      becomes "Loop"→"End", allowing the outro to play and the montage
 *      to finish.
 */
UCLASS()
class DE_API UDESupportAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:

	// -----------------------------------------------------------------------
	// Called by DEGA_Heal
	// -----------------------------------------------------------------------

	/**
	 * Must be called after Montage_Play() so the instance knows which montage
	 * to manipulate.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation|Heal")
	void BeginHealLoop(UAnimMontage* InMontage);

	/**
	 * Called by DEGA_Heal::EndAbility to break out of the loop section.
	 * The montage will finish its current "Loop" iteration, then advance to
	 * the "End" section and play to completion.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation|Heal")
	void StopHealLoop();

	// -----------------------------------------------------------------------
	// Called by AnimNotifies placed in the montage
	// -----------------------------------------------------------------------

	/**
	 * Called by DEAnimNotify_HealLoopStart to update the active montage pointer
	 * to the live instance (which may differ from what was passed to BeginHealLoop
	 * if the ABP replayed the montage on its first tick after bIsHealChanneling was set).
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation|Heal")
	void CacheActiveMontage(UAnimMontage* InMontage) { ActiveHealMontage = InMontage; }

	/** Bound to DEAnimNotify_HealLoopStart — fired at the tail of the "Start" section. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Heal")
	void OnHealLoopStartNotify();

	// -----------------------------------------------------------------------
	// State readable from the Animation Blueprint
	// -----------------------------------------------------------------------

	/** True while DEGA_Heal is actively channeling; drives blend-tree states. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Heal")
	bool bIsHealChanneling = false;

	/** Section name constants — must match the montage asset exactly. */
	static const FName SectionStart;
	static const FName SectionLoop;
	static const FName SectionEnd;

private:

	/** Montage currently being managed; cleared when the montage finishes. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveHealMontage;
};
