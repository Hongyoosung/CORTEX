// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MocAbility.generated.h"

class AMocCharacter;
class AMatchManager;

/**
 * Base class for ability logic (not a component)
 */
UCLASS(Abstract, BlueprintType, EditInlineNew)
class GAMEAI_PROJECT_API UMocAbility : public UObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(AMocCharacter* InOwner);
	
	/** Update logic (called by the component) */
	virtual void TickAbility(float DeltaTime) {}
	
	/** Training mode: self-select target and act */
	virtual void Execute(float DeltaTime) {}

	/** Inference mode: BT task passes explicit target */
	virtual void ExecuteWithTarget(float DeltaTime, AActor* Target) {}

protected:
	UPROPERTY()
	TObjectPtr<AMocCharacter> OwnerCharacter;

	UPROPERTY(Transient)
	TObjectPtr<AMatchManager> CachedMatchManager;

	AMatchManager* GetMatchManager() const;
};
