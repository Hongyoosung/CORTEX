// File: AI/Debug/EQSWeightVisualizer.cpp

void AEQSWeightVisualizer::DrawCurrentWeights(
    const FEQSWeightParameters& Weights
) {
    if (!GEngine) return;
    
    FString WeightText = FString::Printf(
        TEXT("EQS Weights:\n")
        TEXT("Enemy Obj: %.2f | Ally Obj: %.2f\n")
        TEXT("Cover: %.2f | Visibility: %.2f\n")
        TEXT("Ally Prox: %.2f | Range: %.2f\n")
        TEXT("Pickup: %.2f | Height: %.2f"),
        Weights.EnemyObjectiveProximity,
        Weights.AllyObjectiveProximity,
        Weights.CoverDensity,
        Weights.EnemyVisibility,
        Weights.AllyProximity,
        Weights.CombatRange,
        Weights.PickupProximity,
        Weights.HeightAdvantage
    );
    
    GEngine->AddOnScreenDebugMessage(
        -1, 0.1f, FColor::Cyan, WeightText
    );
    
    // EQS 샘플 포인트 시각화
    DrawDebugEQSSamples(GetWorld(), LastQueryResult);
}
