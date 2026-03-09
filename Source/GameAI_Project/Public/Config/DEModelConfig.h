
#pragma once

#include "CoreMinimal.h"

namespace DEModelConfig
{
    // ===== MCTS Limits =====
    
    constexpr int32 MAX_ITERATIONS = 50;


    constexpr float TIME_BUDGET = 0.015f; 

    // ===== Uncertainty Penalties =====
    
    constexpr float RISK_WEIGHT_K = 2.5f;

    // ===== Network Configuration =====
    
    constexpr int32 INPUT_FEATURES = 54;
    

    constexpr int32 HIDDEN_LAYERS = 3;
    

    constexpr int32 HIDDEN_DIM = 256;


    constexpr int32 BATCH_SIZE = 8;
}