#include "Observation/TeamObservation.h"
#include "Simulation/StateTransition.h"
#include "Team/Components/FollowerAgentComponent.h"

TArray<float> FTeamObservation::ToFeatureVector() const
{
    TArray<float> Features;
    Features.Reserve(40 + (FollowerObservations.Num() * 71));

    // ========================================
    // TEAM COMPOSITION (6 features)
    // ========================================

    Features.Add(FMath::Clamp(static_cast<float>(AliveFollowers) / 10.0f, 0.0f, 1.0f));
    Features.Add(FMath::Clamp(static_cast<float>(DeadFollowers) / 10.0f, 0.0f, 1.0f));
    Features.Add(AverageTeamHealth / 100.0f);
    Features.Add(MinTeamHealth / 100.0f);
    Features.Add(AverageTeamStamina / 100.0f);
    Features.Add(AverageTeamAmmo / 100.0f);

    // ========================================
    // TEAM FORMATION (9 features)
    // ========================================

    Features.Add(TeamCentroid.X / 10000.0f);
    Features.Add(TeamCentroid.Y / 10000.0f);
    Features.Add(TeamCentroid.Z / 10000.0f);
    Features.Add(FMath::Clamp(FormationSpread / 5000.0f, 0.0f, 1.0f));
    Features.Add(FormationCoherence);
    Features.Add(FMath::Clamp(AverageDistanceToObjective / 10000.0f, 0.0f, 1.0f));
    Features.Add(TeamFacingDirection.X);
    Features.Add(TeamFacingDirection.Y);
    Features.Add(TeamFacingDirection.Z);

    // ========================================
    // ENEMY INTELLIGENCE (12 features)
    // ========================================

    Features.Add(FMath::Clamp(static_cast<float>(TotalVisibleEnemies) / 20.0f, 0.0f, 1.0f));
    Features.Add(FMath::Clamp(static_cast<float>(EnemiesEngaged) / 10.0f, 0.0f, 1.0f));
    Features.Add(AverageEnemyHealth / 100.0f);
    Features.Add(FMath::Clamp(NearestEnemyDistance / 10000.0f, 0.0f, 1.0f));
    Features.Add(FMath::Clamp(FarthestEnemyDistance / 10000.0f, 0.0f, 1.0f));
    Features.Add(EnemyCentroid.X / 10000.0f);
    Features.Add(EnemyCentroid.Y / 10000.0f);
    Features.Add(EnemyCentroid.Z / 10000.0f);
    Features.Add(FMath::Clamp(static_cast<float>(EstimatedTotalEnemies) / 20.0f, 0.0f, 1.0f));
    Features.Add(FMath::Clamp(TimeSinceLastContact / 60.0f, 0.0f, 1.0f));

    // ========================================
    // TACTICAL SITUATION (8 features)
    // ========================================

    Features.Add(bOutnumbered ? 1.0f : 0.0f);
    Features.Add(bFlanked ? 1.0f : 0.0f);
    Features.Add(bHasCoverAdvantage ? 1.0f : 0.0f);
    Features.Add(bHasHighGround ? 1.0f : 0.0f);
    Features.Add(static_cast<float>(EngagementRange) / 4.0f);
    Features.Add(FMath::Clamp(KillDeathRatio / 5.0f, 0.0f, 1.0f));
    Features.Add(FMath::Clamp(TimeInCurrentState / 60.0f, 0.0f, 1.0f));
    Features.Add(ThreatLevel / 10.0f);

    // ========================================
    // INDIVIDUAL FOLLOWER OBSERVATIONS (N × 66)
    // ========================================

    for (const FObservationElement& FollowerObs : FollowerObservations)
    {
        Features.Append(FollowerObs.ToFeatureVector());
    }

    return Features;
}

void FTeamObservation::Reset()
{
    // Team Composition
    AliveFollowers = 0;
    DeadFollowers = 0;
    AverageTeamHealth = 100.0f;
    MinTeamHealth = 100.0f;
    AverageTeamStamina = 100.0f;
    AverageTeamAmmo = 100.0f;

    // Team Formation
    TeamCentroid = FVector::ZeroVector;
    FormationSpread = 0.0f;
    FormationCoherence = 1.0f;
    AverageDistanceToObjective = 0.0f;
    TeamFacingDirection = FVector::ForwardVector;

    // Enemy Intelligence
    TotalVisibleEnemies = 0;
    EnemiesEngaged = 0;
    AverageEnemyHealth = 100.0f;
    NearestEnemyDistance = 99999.0f;
    FarthestEnemyDistance = 0.0f;
    EnemyCentroid = FVector::ZeroVector;
    EstimatedTotalEnemies = 0;
    TimeSinceLastContact = 0.0f;
    TrackedEnemies.Empty();

    // Tactical Situation
    bOutnumbered = false;
    bFlanked = false;
    bHasCoverAdvantage = false;
    bHasHighGround = false;
    EngagementRange = EEngagementRange::Medium;
    KillDeathRatio = 1.0f;
    TimeInCurrentState = 0.0f;
    ThreatLevel = 0.0f;

    // Follower Observations
    FollowerObservations.Empty();
}

FTeamObservation FTeamObservation::BuildFromTeam(
    const TArray<AActor*>& TeamMembers,
    AActor* ObjectiveActor,
    const TArray<AActor*>& KnownEnemies)
{
    FTeamObservation TeamObs;

    // Build team observation from team members
    // Collect individual observations and aggregate team-level statistics

    int32 AliveCount = 0;
    int32 DeadCount = 0;
    float TotalHealth = 0.0f;

    // Collect individual follower observations
    for (AActor* Member : TeamMembers)
    {
        if (!Member) continue;

        // Get follower component if available
        UFollowerAgentComponent* Follower = Member->FindComponentByClass<UFollowerAgentComponent>();
        if (Follower)
        {
            FObservationElement Obs = Follower->GetLocalObservation();
            TeamObs.FollowerObservations.Add(Obs);

            // Track follower positions for MCTS ally detection
            TeamObs.FollowerPositions.Add(Member, Member->GetActorLocation());

            // Aggregate statistics
            // Note: AgentHealth is 0-100 scale, consider alive if > 0
            if (Obs.AgentHealth > 0.0f)
            {
                AliveCount++;
                TotalHealth += Obs.AgentHealth;
            }
            else
            {
                DeadCount++;
            }
        }
    }

    TeamObs.AliveFollowers = AliveCount;
    TeamObs.DeadFollowers = DeadCount;
    // Calculate average health percentage across all team members (alive + dead)
    int32 TotalMembers = AliveCount + DeadCount;
    TeamObs.AverageTeamHealth = (TotalMembers > 0) ? (TotalHealth / TotalMembers) : 0.0f;

    // Calculate team centroid
    if (TeamMembers.Num() > 0)
    {
        FVector Sum = FVector::ZeroVector;
        for (AActor* Member : TeamMembers)
        {
            if (Member)
            {
                Sum += Member->GetActorLocation();
            }
        }
        TeamObs.TeamCentroid = Sum / TeamMembers.Num();
    }

    // ============================================================================
    // FORMATION COHERENCE CALCULATION
    // ============================================================================
    // Measures tactical spacing quality based on inter-agent distances
    // Optimal spacing: 400-800cm (close enough to support, far enough to avoid clustering)
    // Penalizes: < 200cm (too close, vulnerable to AoE) or > 1500cm (too spread, no mutual support)
    // ============================================================================

    if (TeamMembers.Num() >= 2)
    {
        float TotalScore = 0.0f;
        int32 PairCount = 0;
        float TotalDistance = 0.0f;

        // Calculate pairwise distances and score each pair
        for (int32 i = 0; i < TeamMembers.Num(); ++i)
        {
            AActor* Agent1 = TeamMembers[i];
            if (!Agent1) continue;

            for (int32 j = i + 1; j < TeamMembers.Num(); ++j)
            {
                AActor* Agent2 = TeamMembers[j];
                if (!Agent2) continue;

                float Distance = FVector::Dist(Agent1->GetActorLocation(), Agent2->GetActorLocation());
                TotalDistance += Distance;

                // Score this pair's spacing (0.0 = bad, 1.0 = optimal)
                float PairScore = 0.0f;

                // Optimal range: 400-800cm → score = 1.0
                if (Distance >= 400.0f && Distance <= 800.0f)
                {
                    PairScore = 1.0f;
                }
                // Too close: < 400cm → score decreases (0.0 at ~68cm collision boundary)
                else if (Distance < 400.0f)
                {
                    // Linear falloff from 400cm (score=1.0) to 68cm (score=0.0)
                    PairScore = FMath::Clamp((Distance - 68.0f) / (400.0f - 68.0f), 0.0f, 1.0f);
                }
                // Too spread: > 800cm → score decreases (0.0 at 2000cm)
                else if (Distance > 800.0f)
                {
                    // Linear falloff from 800cm (score=1.0) to 2000cm (score=0.0)
                    PairScore = FMath::Clamp(1.0f - ((Distance - 800.0f) / 1200.0f), 0.0f, 1.0f);
                }

                TotalScore += PairScore;
                PairCount++;
            }
        }

        // Average score across all pairs
        if (PairCount > 0)
        {
            TeamObs.FormationCoherence = TotalScore / PairCount;

            // Also calculate formation spread (max distance between any two agents)
            TeamObs.FormationSpread = TotalDistance / PairCount; // Average pairwise distance

            // Log formation quality for diagnosis
            UE_LOG(LogTemp, Log, TEXT("[FORMATION QUALITY] AvgDistance: %.1f cm, Coherence: %.3f (%.1f pairs scored, optimal: 400-800cm)"),
                TeamObs.FormationSpread,
                TeamObs.FormationCoherence,
                static_cast<float>(PairCount));
        }
    }
    else if (TeamMembers.Num() == 1)
    {
        // Single agent: perfect coherence (no spacing issues)
        TeamObs.FormationCoherence = 1.0f;
        TeamObs.FormationSpread = 0.0f;
    }

    // Track enemies
    TeamObs.TotalVisibleEnemies = KnownEnemies.Num();
    TeamObs.TrackedEnemies.Append(KnownEnemies);

    // Calculate objective distance
    if (ObjectiveActor)
    {
        TeamObs.DistanceToObjective = FVector::Dist(TeamObs.TeamCentroid, ObjectiveActor->GetActorLocation());
    }

    return TeamObs;
}

float FTeamObservation::CalculateSimilarity(
    const FTeamObservation& A,
    const FTeamObservation& B)
{
    // Weighted feature comparison
    float HealthDiff = FMath::Abs(A.AverageTeamHealth - B.AverageTeamHealth) / 100.0f;
    float FormationDiff = FMath::Abs(A.FormationCoherence - B.FormationCoherence);
    float EnemyDiff = FMath::Abs(A.TotalVisibleEnemies - B.TotalVisibleEnemies) / 20.0f;
    float PositionDiff = FVector::Dist(A.TeamCentroid, B.TeamCentroid) / 10000.0f;

    // Weighted average
    float WeightedDiff =
        0.3f * HealthDiff +
        0.25f * FormationDiff +
        0.25f * EnemyDiff +
        0.2f * PositionDiff;

    // Convert difference to similarity (exponential decay)
    return FMath::Exp(-WeightedDiff * 5.0f);  // [0, 1], higher = more similar
}

TArray<float> FTeamObservation::Flatten() const
{
    // Same as ToFeatureVector, but explicitly named for world model usage
    return ToFeatureVector();
}

FTeamObservation FTeamObservation::ApplyDelta(const FTeamStateDelta& Delta) const
{
    FTeamObservation NextState = *this;

    // Apply team-level deltas
    NextState.AverageTeamHealth = FMath::Clamp(AverageTeamHealth + Delta.TeamHealthDelta, 0.0f, 100.0f);
    NextState.AliveFollowers = FMath::Max(0, AliveFollowers + Delta.AliveCountDelta);
    NextState.DeadFollowers = FMath::Max(0, DeadFollowers - Delta.AliveCountDelta);
    NextState.FormationCoherence = FMath::Clamp(FormationCoherence + Delta.TeamCohesionDelta, 0.0f, 1.0f);

    // Apply individual agent deltas
    for (int32 i = 0; i < FMath::Min(NextState.FollowerObservations.Num(), Delta.AgentDeltas.Num()); ++i)
    {
        const FAgentStateDelta& AgentDelta = Delta.AgentDeltas[i];
        FObservationElement& FollowerObs = NextState.FollowerObservations[i];

        // Apply health change
        FollowerObs.AgentHealth = FMath::Clamp(FollowerObs.AgentHealth + AgentDelta.HealthDelta, 0.0f, 100.0f);

        // Apply position change (relative to current centroid)
        // FollowerObs.Position += AgentDelta.PositionDelta;

        // Apply ammo change
        // FollowerObs.CurrentAmmo = FMath::Max(0, FollowerObs.CurrentAmmo + AgentDelta.AmmoDelta);

        // Apply status changes
        if (AgentDelta.bIsDead)
        {
            FollowerObs.AgentHealth = 0.0f;
        }
    }

    // Update combat metrics
    // Note: These are estimates, actual values would come from simulation
    const int32 PreviousKills = 0; // Would need to track this
    const int32 PreviousDeaths = DeadFollowers;

    return NextState;
}

FTeamObservation FTeamObservation::Clone() const
{
    // Deep copy all fields
    FTeamObservation Cloned = *this;

    // TrackedEnemies is a TSet, needs explicit copy
    Cloned.TrackedEnemies.Empty();
    for (AActor* Enemy : TrackedEnemies)
    {
        Cloned.TrackedEnemies.Add(Enemy);
    }

    // FollowerObservations is a TArray, copy should be automatic
    // but we can be explicit
    Cloned.FollowerObservations = FollowerObservations;

    return Cloned;
}

FString FTeamObservation::Serialize() const
{
    FString Json = TEXT("{\n");

    // Team composition
    Json += FString::Printf(TEXT("  \"alive_followers\": %d,\n"), AliveFollowers);
    Json += FString::Printf(TEXT("  \"dead_followers\": %d,\n"), DeadFollowers);
    Json += FString::Printf(TEXT("  \"average_team_health\": %.2f,\n"), AverageTeamHealth);
    Json += FString::Printf(TEXT("  \"min_team_health\": %.2f,\n"), MinTeamHealth);
    Json += FString::Printf(TEXT("  \"average_team_stamina\": %.2f,\n"), AverageTeamStamina);
    Json += FString::Printf(TEXT("  \"average_team_ammo\": %.2f,\n"), AverageTeamAmmo);

    // Team formation
    Json += FString::Printf(TEXT("  \"team_centroid\": [%.2f, %.2f, %.2f],\n"),
        TeamCentroid.X, TeamCentroid.Y, TeamCentroid.Z);
    Json += FString::Printf(TEXT("  \"formation_spread\": %.2f,\n"), FormationSpread);
    Json += FString::Printf(TEXT("  \"formation_coherence\": %.2f,\n"), FormationCoherence);
    Json += FString::Printf(TEXT("  \"average_distance_to_objective\": %.2f,\n"), AverageDistanceToObjective);

    // Enemy intelligence
    Json += FString::Printf(TEXT("  \"total_visible_enemies\": %d,\n"), TotalVisibleEnemies);
    Json += FString::Printf(TEXT("  \"enemies_engaged\": %d,\n"), EnemiesEngaged);
    Json += FString::Printf(TEXT("  \"average_enemy_health\": %.2f,\n"), AverageEnemyHealth);
    Json += FString::Printf(TEXT("  \"nearest_enemy_distance\": %.2f,\n"), NearestEnemyDistance);

    // Tactical situation
    Json += FString::Printf(TEXT("  \"outnumbered\": %s,\n"), bOutnumbered ? TEXT("true") : TEXT("false"));
    Json += FString::Printf(TEXT("  \"flanked\": %s,\n"), bFlanked ? TEXT("true") : TEXT("false"));
    Json += FString::Printf(TEXT("  \"cover_advantage\": %s,\n"), bHasCoverAdvantage ? TEXT("true") : TEXT("false"));
    Json += FString::Printf(TEXT("  \"high_ground\": %s,\n"), bHasHighGround ? TEXT("true") : TEXT("false"));
    Json += FString::Printf(TEXT("  \"threat_level\": %.2f,\n"), ThreatLevel);

    // Flattened feature vector
    TArray<float> Features = ToFeatureVector();
    Json += TEXT("  \"features\": [");
    for (int32 i = 0; i < Features.Num(); ++i)
    {
        Json += FString::Printf(TEXT("%.4f"), Features[i]);
        if (i < Features.Num() - 1) Json += TEXT(", ");
    }
    Json += TEXT("]\n");

    Json += TEXT("}");

    return Json;
}
