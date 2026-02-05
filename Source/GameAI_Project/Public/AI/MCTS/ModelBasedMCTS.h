#pragma once

#include "CoreMinimal.h"
#include "AI/Options/TacticalOption.h"
#include "AI/Models/LearnedWorldModel.h"
#include "AI/MCTS/TreeNode.h"
#include "ModelBasedMCTS.generated.h"

/**
 * MCTS 설정 구조체
 * Design Doc v10.1: Time Budget 15ms, Batch Size 8
 */
struct FMCTSConfig
{
    float TimeBudgetSeconds = 0.015f; //
    int32 BatchSize = 8;              //
    int32 MaxIterations = 50;         // 안전장치용 최대 반복 횟수
};

/**
 * 성향 기반 보상 가중치 (Scalarization용)
 */
struct FAgentPersonality
{
    float WinWeight = 1.0f;
    float SurvivalWeight = 0.5f;
    float ObjectiveWeight = 1.5f;
    float EfficiencyWeight = 0.2f;
};

/**
 * CORTEX v10.1: Model-Based MCTS Planner
 * AlphaZero 스타일의 계획 수립기입니다.
 * 물리 시뮬레이션 대신 World Model을 사용하여 미래를 예측하고, 배치 처리를 통해 성능을 최적화합니다.
 */
UCLASS()
class GAMEAI_PROJECT_API UModelBasedMCTS : public UObject
{
    GENERATED_BODY()

public:
    UModelBasedMCTS();

    /** 의존성 주입: World Model 설정 */
    void Setup(ULearnedWorldModel* InWorldModel);

    /**
     * 메인 진입점: 현재 상태에서 최적의 전술적 옵션을 찾습니다.
     * 15ms 예산 내에서 실행됩니다.
     */
    FTacticalOption FindBestOption(const FObservation& CurrentState);

private:
    // ===== Core MCTS Phases =====

    /**
     * 1. Selection Phase
     * 루트에서 시작하여 아직 확장되지 않은(Unexpanded) 리프 노드를 찾습니다.
     * Virtual Loss를 적용하여 동일 배치 내 중복 선택을 방지합니다.
     */
    TSharedPtr<FTreeNode> SelectNode(TSharedPtr<FTreeNode> Node);

    /**
     * 2. Expansion Preparation
     * 선택된 리프 노드에서 수행 가능한 후보 옵션(Candidate Options)을 생성합니다.
     */
    TArray<FTacticalOption> GenerateCandidateOptions(const FObservation& State);

    /**
     * 3. Batch Inference & Expansion
     * 수집된 (Node, Option) 쌍들을 World Model에 배치로 입력하고 트리를 확장합니다.
     */
    void ProcessBatch(TArray<TPair<TSharedPtr<FTreeNode>, FTacticalOption>>& NodesToExpand);

    /**
     * 4. Multi-Objective Scalarization
     * 벡터 형태의 보상(승률, 체력, 목표)을 단일 스칼라 값(Value)으로 변환합니다.
     */
    float ScalarizeReward(const FCompositeReward& Reward) const;

private:
    UPROPERTY()
    ULearnedWorldModel* WorldModel;

    FMCTSConfig Config;
    FAgentPersonality CurrentPersonality; // 현재 에이전트의 성향
    
    // 재사용 메모리 풀 (GC 부하 방지)
    TArray<TPair<TSharedPtr<FTreeNode>, FTacticalOption>> PendingBatch;
};