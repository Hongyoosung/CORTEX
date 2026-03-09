// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "DEAbility.generated.h"

class ADECharacter;
class ADEMatchManager;

/**
 * Base class for ability logic (not a component)
 */
UCLASS(Abstract, BlueprintType, EditInlineNew)
class GAMEAI_PROJECT_API UDEAbility : public UObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(ADECharacter* InOwner);
	
	/** Update logic (called by the component) */
	virtual void TickAbility(float DeltaTime) {}
	
	/** Training mode: self-select target and act */
	virtual void Execute(float DeltaTime) {}

	/** Inference mode: BT task passes explicit target */
	virtual void ExecuteWithTarget(float DeltaTime, AActor* Target) {}

protected:
	UPROPERTY()
	TObjectPtr<ADECharacter> OwnerCharacter;

	UPROPERTY(Transient)
	mutable TObjectPtr<ADEMatchManager> CachedMatchManager;

	ADEMatchManager* GetMatchManager() const;
};
