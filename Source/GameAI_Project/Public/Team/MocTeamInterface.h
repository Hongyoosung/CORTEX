#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MocTeamInterface.generated.h"

// Blueprint에서도 호출 가능하도록 UInterface 선언
UINTERFACE(MinimalAPI, Blueprintable)
class UMocTeamInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 팀 소유권 및 식별을 위한 공통 인터페이스
 */
class GAMEAI_PROJECT_API IMocTeamInterface
{
	GENERATED_BODY()

public:
	/** 팀 ID 반환 (0: Red, 1: Blue, -1: Neutral/None) */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Team")
	int32 GetTeamID() const;


	/** 상대방이 적인지 확인하는 유틸리티 함수 (기본 구현 제공 가능) */
	virtual bool IsEnemy(AActor* Other) const;

    
    /** 상대방이 아군인지 확인 */
	virtual bool IsAlly(AActor* Other) const;
};