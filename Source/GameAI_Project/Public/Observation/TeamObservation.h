#pragma once

#include "CoreMinimal.h"
#include "ObservationElement.h"
#include "TeamObservationTypes.h"
#include "Team/Mission.h"
#include "TeamObservation.generated.h"

/**
 * Team-level observation for strategic MCTS
 * Combines aggregate team metrics with individual follower observations
 * Total features: 40 (base) + N×71 (per follower)
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FTeamObservation
{
    GENERATED_BODY()

    //--------------------------------------------------------------------------
    // TEAM COMPOSITION (6 features)
    //--------------------------------------------------------------------------

    /** Number of alive team members */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Composition")
    int32 AliveFollowers = 0;

    /** Number of dead team members */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Composition")
    int32 DeadFollowers = 0;

    /** Average team health (0-100) */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Composition")
    float AverageTeamHealth = 100.0f;

    /** Minimum team member health (identifies weakest link) */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Composition")
    float MinTeamHealth = 100.0f;

    /** Average team stamina */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Composition")
    float AverageTeamStamina = 100.0f;

    /** Average team ammunition percentage */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Composition")
    float AverageTeamAmmo = 100.0f;

    //--------------------------------------------------------------------------
    // TEAM FORMATION (9 features)
    //--------------------------------------------------------------------------

    /** Team centroid position (geometric center) */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Formation")
    FVector TeamCentroid = FVector::ZeroVector;  // 3 features

    /** Formation spread (std deviation of positions) */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Formation")
    float FormationSpread = 0.0f;

    /** Formation coherence (0-1, higher = tighter) */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Formation")
    float FormationCoherence = 1.0f;

    /** Average distance to objective */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Formation")
    float AverageDistanceToObjective = 0.0f;

    /** Team average facing direction (normalized) */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Formation")
    FVector TeamFacingDirection = FVector::ForwardVector;  // 3 features

    //--------------------------------------------------------------------------
    // ENEMY INTELLIGENCE (12 features)
    //--------------------------------------------------------------------------

    /** Total visible enemies (across all team members) */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Enemies")
    int32 TotalVisibleEnemies = 0;

    /** Enemies currently engaged in combat */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Enemies")
    int32 EnemiesEngaged = 0;

    /** Average enemy health */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Enemies")
    float AverageEnemyHealth = 100.0f;

    /** Distance to nearest enemy */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Enemies")
    float NearestEnemyDistance = 99999.0f;

    /** Distance to farthest enemy */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Enemies")
    float FarthestEnemyDistance = 0.0f;

    /** Enemy centroid position (geometric center of enemies) */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Enemies")
    FVector EnemyCentroid = FVector::ZeroVector;  // 3 features

    /** Estimated total enemy count (including unseen) */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Enemies")
    int32 EstimatedTotalEnemies = 0;

    /** Time since last enemy contact (seconds) */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Enemies")
    float TimeSinceLastContact = 0.0f;

    /** Unique enemy actors tracked */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Enemies")
    TSet<AActor*> TrackedEnemies;

    //--------------------------------------------------------------------------
    // TACTICAL SITUATION (8 features)
    //--------------------------------------------------------------------------

    /** Are we outnumbered? */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Tactical")
    bool bOutnumbered = false;

    /** Are we flanked? */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Tactical")
    bool bFlanked = false;

    /** Do we have cover advantage? */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Tactical")
    bool bHasCoverAdvantage = false;

    /** Do we have high ground? */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Tactical")
    bool bHasHighGround = false;

    /** Current engagement range category */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Tactical")
    EEngagementRange EngagementRange = EEngagementRange::Medium;

    /** Team kill/death ratio this encounter */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Tactical")
    float KillDeathRatio = 1.0f;

    /** Time spent in current strategic state (seconds) */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Tactical")
    float TimeInCurrentState = 0.0f;

    /** Current threat level assessment (0-10) */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Tactical")
    float ThreatLevel = 0.0f;

    //--------------------------------------------------------------------------
    // MISSION CONTEXT (5 features)
    //--------------------------------------------------------------------------

    /** Distance to primary objective */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Mission")
    float DistanceToObjective = 0.0f;

    /** Current Mission type */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Mission")
    EMissionType MissionType = EMissionType::None;

    /** Mission time remaining (0 = no limit) */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Mission")
    float MissionTimeRemaining = 0.0f;

    /** Current mission phase */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Mission")
    EMissionPhase MissionPhase = EMissionPhase::Approach;

    /** Estimated mission difficulty (0-10) */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Mission")
    float EstimatedDifficulty = 5.0f;

    //--------------------------------------------------------------------------
    // INDIVIDUAL FOLLOWER OBSERVATIONS (N × 71 features)
    //--------------------------------------------------------------------------

    /** Individual observations for each follower */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Followers")
    TArray<FObservationElement> FollowerObservations;

    /** Map of follower actors to their positions (for MCTS ally tracking) */
    UPROPERTY(BlueprintReadOnly, Category = "Team|Followers")
    TMap<AActor*, FVector> FollowerPositions;

    //--------------------------------------------------------------------------
    // UTILITY FUNCTIONS
    //--------------------------------------------------------------------------

    /** Convert to feature vector for MCTS/NN */
    TArray<float> ToFeatureVector() const;

    /** Convert to RL observation (alias for ToFeatureVector for curriculum learning) */
    TArray<float> ToRLObservation() const { return ToFeatureVector(); }

    /** Get total feature count: 40 + (N × 71) */
    int32 GetFeatureCount() const
    {
        return 40 + (FollowerObservations.Num() * 71);
    }

    /** Reset to default values */
    void Reset();

    /** Build team observation from array of agents */
    static FTeamObservation BuildFromTeam(
        const TArray<AActor*>& TeamMembers,
        AActor* ObjectiveActor = nullptr,
        const TArray<AActor*>& KnownEnemies = TArray<AActor*>()
    );

    /** Calculate observation similarity (for MCTS tree reuse) */
    static float CalculateSimilarity(
        const FTeamObservation& A,
        const FTeamObservation& B
    );

    /** Flatten team observation to feature vector (for world model) */
    TArray<float> Flatten() const;

    /** Apply state delta to create predicted next state */
    FTeamObservation ApplyDelta(const struct FTeamStateDelta& Delta) const;

    /** Deep clone observation */
    FTeamObservation Clone() const;

    /** Serialize to JSON for world model training */
    FString Serialize() const;
};
