#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DETeamInterface.generated.h"


UINTERFACE(MinimalAPI, Blueprintable)
class UDETeamInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * A common interface for team ownership and identification
 */
class DE_API IDETeamInterface
{
	GENERATED_BODY()

public:
	/** Return Team ID (0: Red, 1: Blue, -1: Neutral/None) */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "ID")
	int32 GetTeamID() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "ID")
	int32 GetEnvID() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "ID")
	void SetTeamID(int32 NewTeamID);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "ID")
	void SetEnvID(int32 NewEnvID);


	virtual bool IsEnemy(AActor* Other) const;

	virtual bool IsAlly(AActor* Other) const;
};