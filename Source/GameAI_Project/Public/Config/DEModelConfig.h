
#pragma once

#include "CoreMinimal.h"

namespace DEModelConfig
{
    // ===== MCTS Limits =====
    
    /** AlphaZero 스타일은 더 적지만 스마트한 반복을 수행합니다. */
    constexpr int32 MAX_ITERATIONS = 50;

    /** 실시간 프레임 예산 (15ms) */
    constexpr float TIME_BUDGET = 0.015f; 

    // ===== Uncertainty Penalties =====
    
    /** 신뢰도가 낮은 예측에 대한 페널티 가중치 (Hallucination 방지) */
    constexpr float RISK_WEIGHT_K = 2.5f;

    // ===== Network Configuration =====
    
    /** 입력 피처 수 (52 State + 4 Option Context 등) */
    constexpr int32 INPUT_FEATURES = 54;
    
    /** 은닉층 개수 */
    constexpr int32 HIDDEN_LAYERS = 3;
    
    /** 은닉층 차원 크기 */
    constexpr int32 HIDDEN_DIM = 256;

    /** 배치 추론 크기 (Leaf Parallelism) */
    constexpr int32 BATCH_SIZE = 8;
}