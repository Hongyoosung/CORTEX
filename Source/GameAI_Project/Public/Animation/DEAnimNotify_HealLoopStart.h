// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "DEAnimNotify_HealLoopStart.generated.h"

/**
 * UDEAnimNotify_HealLoopStart
 *
 * Place this notify at the very end of the "Start" section in the heal montage.
 * When fired it calls UDESupportAnimInstance::OnHealLoopStartNotify(), which
 * redirects the "Loop" section to loop back on itself.
 */
UCLASS(meta = (DisplayName = "DE Heal Loop Start"))
class GAMEAI_PROJECT_API UDEAnimNotify_HealLoopStart : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp,
	                    UAnimSequenceBase* Animation,
	                    const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override
	{
		return TEXT("StartLoop");
	}
};
