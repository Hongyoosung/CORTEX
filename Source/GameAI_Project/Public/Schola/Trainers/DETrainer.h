// File: Schola/Trainers/DETrainer.h

#pragma once

#include "CoreMinimal.h"
#include "Training/AbstractTrainer.h"
#include "Perception/AIPerceptionComponent.h"
#include "Types/DERewardTypes.h"
#include "Types/DEEQSTypes.h"
#include "Types/DEObservationTypes.h"
#include "Types/DEStrategyTypes.h"
#include "DETrainer.generated.h"

class UDEScholaAgent;
class UDEScholaTransitionLogger;
class ADECharacter;
class ADECapturePoint;
class ADEMatchManager;
class ADEScholaEnvironment;

/**
 * Schola Trainer
 *
 * Responsibilities:
 * - Train Executor Agent RL Policy (Translates Commander's strategy into EQS weights)
 * - Bridge between Python (RLlib/Gym) and UE5
 * - Policy training conditioned on Commanded Strategy
 * - Action collection and application in the form of EQS weights
 * - Team-level reward calculation based on assigned strategy from Squad Commander
 *
 * Action Space: Box(6) - Continuous EQS weights [-1, 1]
 * Observation Space: Box(52) - Agent State only (Strategy is commanded externally)
 */
UCLASS(Blueprintable)
class GAMEAI_PROJECT_API ADETrainer : public AAbstractTrainer
{
    GENERATED_BODY()

public:
    //=========================================
    // Lifecycle & Initialization
    //=========================================

    ADETrainer();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    /** Initialize the trainer after spawning */
    void InitializeDETrainer(UDEScholaAgent* InAgent);


    //=========================================
    // AAbstractTrainer Overrides
    //=========================================

    virtual float ComputeReward() override;
    virtual EAgentTrainingStatus ComputeStatus() override;
    virtual void GetInfo(TMap<FString, FString>& Info) override;
    virtual void ResetTrainer() override;
    virtual void OnCompletion() override;


    //=========================================
    // Schola RL Interface
    //=========================================

    /**
     * Returns current observation (sent to Python)
     * @return 52-dim vector: Agent state
     */
    TArray<float> GetObservation();

    /** Checks if the current episode should terminate */
    bool IsEpisodeDone();

    /** Legacy Schola interface for episode reset */
    void ResetEpisode();


    //=========================================
    // Debug & Utility Functions
    //=========================================

    /** Returns current training statistics */
    UFUNCTION(BlueprintCallable, Category = "Training|Stats")
    void GetTrainingStats(int32& OutEpisodes, float& OutAvgReward, float& OutAvgLength) const;

    /** Logs detailed reward breakdown to console */
    UFUNCTION(BlueprintCallable, Category = "Debug")
    void LogRewardBreakdown() const;

    /** Draws visual debug information in the world */
    UFUNCTION(BlueprintCallable, Category = "Debug")
    void DrawTrainingDebug(float DeltaTime);

    /** Validates if the EQS weights are within valid ranges */
    bool ValidateEQSWeights(const FDEEQSWeightParameters& Weights) const;

    /** Validates integrity of the gathered observation */
    bool ValidateObservation(const FDEObservation& Obs) const;



protected:
    //=========================================
    // Internal Helper Functions (Private/Protected Logic)
    //=========================================

    /** Collects 52-dim state observation from the environment */
    FDEObservation GatherStateObservation();

    /** Computes team-aligned reward based on commanded strategy quality */
    float ComputeCommandedStrategyReward(EDEStrategyType CommandedStrategy,
        const FDEObservation& Prev,
        const FDEObservation& Current,
        const FDEEQSWeightParameters& Action);

    /** Logs transitions for World Model offline training */
    void LogTransition(const FDEObservation& InState,
        EDEStrategyType CommandedStrategy,
        const FDEEQSWeightParameters& Action,
        float Reward,
        const FDEObservation& NextState,
        bool bDone);

    /** Updates internal training metrics and averages */
    void UpdateTrainingStatistics();



public:
    //=========================================
    // Public Training Configuration
    //=========================================

    /** Maximum steps allowed per episode */
    UPROPERTY(EditAnywhere, Category = "Training")
    int32 MaxEpisodeSteps = 3000;

    /** Path to save transition logs */
    UPROPERTY(EditAnywhere, Category = "Training|Logging")
    FString TransitionLogPath = TEXT("Saved/Transitions/cortex_v10.2_executor.json");

    /** Toggle for transition logging (World Model training) */
    UPROPERTY(EditAnywhere, Category = "Training|Logging")
    bool bLogTransitions = true;

    /** Toggle for visual debug helpers */
    UPROPERTY(EditAnywhere, Category = "Debug")
    bool bEnableDebugVisualization = false;



protected:
    //=========================================
    // Internal State - Actor & Component References
    //=========================================

    /** Reference to the Schola Agent */
    UPROPERTY()
    UDEScholaAgent* DEAgent;

    /** The character being controlled by this trainer */
    UPROPERTY()
    ADECharacter* ControlledCharacter;

    /** Cached match manager for O(1) ally/enemy access */
    UPROPERTY()
    ADEMatchManager* CachedMatchManager;

    /** Cached environment owner for match state queries */
    UPROPERTY()
    ADEScholaEnvironment* CachedScholaEnvironment = nullptr;

    /** Transition logger for data collection */
    UPROPERTY()
    UDEScholaTransitionLogger* TransitionLogger;

    /** Cached capture point references for observations */
    UPROPERTY()
    TArray<ADECapturePoint*> CachedCapturePoints;


    //=========================================
    // Internal State - RL Data & Observations
    ///=========================================

    /** Observations for reward calculation */
    FDEObservation PreviousObservation;
    FDEObservation CurrentObservation;

    /** Most recent action applied */
    FDEEQSWeightParameters LastAction;

    /** Current strategy commanded by the Squad Commander */
    EDEStrategyType CachedCommandedStrategy;


    //=========================================
    // Internal State - Training Metrics & Rewards
    //=========================================

    /** Lifetime cumulative reward (monotonically increasing) */
    float CumulativeLifetimeReward = 0.0f;

    /** Cumulative reward for the current episode */
    float EpisodeReward = 0.0f;

    /** Cached reward from the last action step */
    float CachedStepReward = 0.0f;

    /** Step counter for the current episode */
    int32 CurrentEpisodeSteps = 0;

    /** Flag indicating a new reward is ready for consumption */
    bool bHasNewReward = false;

    /** Diagnostics for post-respawn state */
    bool bWasDeadLastTick = false;


    //=========================================
    // Internal State - Global Statistics & Watchdogs
    //=========================================

    /** Total number of episodes completed */
    int32 TotalEpisodes = 0;

    /** Cumulative reward across all episodes */
    float CumulativeReward = 0.0f;

    /** Rolling average of episode lengths */
    float AverageEpisodeLength = 0.0f;

    /** Ticks elapsed since last new action was received */
    int32 TicksWithoutNewWeights = 0;

    /** Threshold for freeze warning logs (~5s at 60Hz) */
    static constexpr int32 FreezeWatchdogInterval = 300;
};