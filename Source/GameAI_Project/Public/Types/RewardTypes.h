// File: AI/Common/RewardTypes.h
#pragma once

#include "CoreMinimal.h"
#include "StrategyTypes.h"
#include "RewardTypes.generated.h"


/**
 * Reward event types for logging and analysis
 */
UENUM(BlueprintType)
enum class ERewardEventType : uint8
{
    Kill,
    Assist,
    Death,
    CapturePoint,
    LosePoint,
    Survival,
    DistanceShaping,
    TeamVictory,
    StrategyDiversity,
    HealAlly,
    ZoneDurability,
    ZoneGuardKill,
    TeamWipe
};


USTRUCT(BlueprintType)
struct FRewardEvent
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    ERewardEventType EventType;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    EStrategyType Strategy;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    float RewardValue;

    FRewardEvent() : EventType(ERewardEventType::Kill), Strategy(EStrategyType::Assault), RewardValue(0.0f) {}
    FRewardEvent(ERewardEventType InEventType, EStrategyType InStrategy, float InRewardValue)
        : EventType(InEventType), Strategy(InStrategy), RewardValue(InRewardValue) {}
};


/**
 * 에이전트의 보상 상태를 추적하는 구조체 (에피소드마다 초기화)
 */
USTRUCT(BlueprintType)
struct FRewardState
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    float CumulativeReward = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TArray<FRewardEvent> EventLog;

    int32 PostCaptureMomentumStepsRemaining = 0;
    FVector LastCapturedPointLocation = FVector::ZeroVector;
    int32 AssaultZoneStepsAfterCapture = 0;
    int32 CachedInjuredAllyIdx = -1;
    int32 InjuredAllyStalenessCounter = 0;

    // B4: Per-capture-point cooldown for loss penalties
    float LastCaptureLossPenaltyTime;

    bool bSparseKillFiredThisStep = false;
    bool bInFriendlyZone = false;
    float LastIndividualStepReward = 0.0f;
    int32 IsolatedConsecutiveSteps = 0;

    void Reset()
    {
        CumulativeReward = 0.0f;
        EventLog.Empty();
        PostCaptureMomentumStepsRemaining = 0;
        LastCapturedPointLocation = FVector::ZeroVector;
        AssaultZoneStepsAfterCapture = 0;
        CachedInjuredAllyIdx = -1;
        InjuredAllyStalenessCounter = 0;
        LastCaptureLossPenaltyTime = 0;
        bSparseKillFiredThisStep = false;
        bInFriendlyZone = false;
        LastIndividualStepReward = 0.0f;
        IsolatedConsecutiveSteps = 0;
    }
};





USTRUCT(BlueprintType)
struct FAssaultRewardSettings
{
    GENERATED_BODY()

    //========== Combat Properties =============

    UPROPERTY(EditAnywhere, Category = "Combat")
    float SurvivalRewardScale = 1.5f;

    /** Assault: +10 per kill (base KillReward=10 × 1.0) */
    UPROPERTY(EditAnywhere, Category = "Combat")
    float KillRewardScale = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float HealthPenalty = 0.0f;

    /** Assault: -20 per death (base DeathPenaltyReward=100 × 0.2) */
    UPROPERTY(EditAnywhere, Category = "Combat")
    float DeathScale = 0.2f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float IdlePenalty = 1.5f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float TimePenalty = 0.01f;


    //========== Capture Properties =============

    /** Assault: +15 per capture (base CaptureReward=100 × 0.15) */
    UPROPERTY(EditAnywhere, Category = "Capture")
    float CaptureRewardScale = 0.15f;

    /** Assault: -25 per loss (base LossCaptureReward=-100 × 0.25) */
    UPROPERTY(EditAnywhere, Category = "Capture")
    float LossCaptureRewardScale = 0.25f;

    UPROPERTY(EditAnywhere, Category = "Capture")
    float ObjectiveProgressReward = 0.15f;

    UPROPERTY(EditAnywhere, Category = "Capture")
    float ZonePresenceBonus = 3.0f;

    /** Per-step bonus scaled by capture progress [0,1] while actively capping a non-friendly point.
     *  Rewards staying in the zone during the conversion, not just touching it. */
    UPROPERTY(EditAnywhere, Category = "Capture")
    float ActiveCappingBonus = 3.5f;

    UPROPERTY(EditAnywhere, Category = "Capture")
    float PostCaptureMomentumBonus = 3.0f;

    UPROPERTY(EditAnywhere, Category = "Capture")
    int32 PostCaptureMomentumDuration = 30;

    UPROPERTY(EditAnywhere, Category = "Capture")
    float PostCaptureMomentumMinMove = 100.0f;


    //========== Movement Properties =============

    UPROPERTY(EditAnywhere, Category = "Movement")
    float PenaltyPerMeterScale = 1.0f;
};


USTRUCT(BlueprintType)
struct FDefendRewardSettings
{
    GENERATED_BODY()

    //========== Combat Properties =============

    UPROPERTY(EditAnywhere, Category = "Combat")
    float SurvivalRewardScale = 1.5f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float PositionReward = 0.01f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float HealthBonus = 1.0f;

    /** Defend: +1 per kill (base KillReward=10 × 0.1) — kills are incidental, not the objective */
    UPROPERTY(EditAnywhere, Category = "Combat")
    float KillRewardScale = 0.1f;

    /** Defend: -15 per death (base DeathPenaltyReward=100 × 0.15) */
    UPROPERTY(EditAnywhere, Category = "Combat")
    float DeathScale = 0.15f;

    //========== Capture Properties =============

    /** Defend: +20 per capture — core objective (base CaptureReward=100 × 0.20) */
    UPROPERTY(EditAnywhere, Category = "Capture")
    float CaptureRewardScale = 0.20f;

    /** Defend: -30 per loss — critical failure (base LossCaptureReward=-100 × 0.30) */
    UPROPERTY(EditAnywhere, Category = "Capture")
    float LossCaptureRewardScale = 0.30f;

    /** Flat bonus awarded each step the agent is physically inside a friendly capture zone */
    UPROPERTY(EditAnywhere, Category = "Capture")
    float ZonePresenceBonus = 3.0f;

    /** Additional bonus per step when an enemy is actively contesting a friendly zone.
     *  Rewards the agent for being in the right place when it actually matters. */
    UPROPERTY(EditAnywhere, Category = "Capture")
    float ThreatResponseBonus = 3.5f;

    /** Per-cm shaping reward for approaching the nearest friendly capture zone (when outside it).
     *  FIX (Issue 3): Raised from 0.002 to 0.01 (= 1.0 per metre).
     *  At 0.002/cm the approach gradient was dominated by PositionReward (2.0 flat for
     *  standing still anywhere), so the agent learned to idle instead of navigating to zone. */
    UPROPERTY(EditAnywhere, Category = "Capture")
    float ZoneApproachReward = 0.05f;


    //========== Zone Defense Skills =============

    /** Reward per normalized HP absorbed (0–1) while standing inside a friendly capture zone.
     *  Incentivises tanking damage at the zone instead of retreating when shot. */
    UPROPERTY(EditAnywhere, Category = "ZoneDefense")
    float ZoneDurabilityBonus = 2.5f;

    /** Extra kill reward when the killed enemy was within ZoneGuardRadius × CaptureRadius
     *  of any friendly capture point the step before the kill.
     *  Gives a directional kill signal: eliminate threats *to your zone*, not random enemies. */
    UPROPERTY(EditAnywhere, Category = "ZoneDefense")
    float ZoneGuardKillBonus = 3.0f;

    /** Multiplier on CaptureRadius used to define "near the zone" for ZoneGuardKillBonus.
     *  Default 2.0 → enemies within 2× capture radius count as zone threats. */
    UPROPERTY(EditAnywhere, Category = "ZoneDefense")
    float ZoneGuardRadius = 2.0f;


    //========== Movement Properties =============

    UPROPERTY(EditAnywhere, Category = "Movement")
    float PenaltyPerMeterScale = 1.0f;
};


USTRUCT(BlueprintType)
struct FSupportRewardSettings
{
    GENERATED_BODY()

    //========== Combat Properties =============

    UPROPERTY(EditAnywhere, Category = "Combat")
    float SurvivalRewardScale = 1.5f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float PositionReward = 0.0f;

    UPROPERTY(EditAnywhere, Category = "Combat")
    float HealthBonus = 0.3f;

    /** Support: 0 per kill — kills are irrelevant to role; heal rewards are the objective */
    UPROPERTY(EditAnywhere, Category = "Combat")
    float KillRewardScale = 0.0f;

    /** Penalty applied when support agent gets a kill while any ally is below 50% HP.
     *  Discourages role-breaking combat at the expense of healing duty. */
    UPROPERTY(EditAnywhere, Category = "Combat")
    float RoleBreakPenalty = 3.0f;

    /** Support: -10 per death (base DeathPenaltyReward=100 × 0.10) */
    UPROPERTY(EditAnywhere, Category = "Combat")
    float DeathScale = 0.10f;


    //========== Ally Proximity Properties =============

    /** Flat bonus per step for being within SupportAllyProximityThreshold of the most-injured ally.
     *  Only fires when the target ally's HP is below AllyInjuryThreshold (normalized 0-1).
     *  Prevents support from permanently camping with fully-healthy allies. */
    UPROPERTY(EditAnywhere, Category = "AllyProximity")
    float AllyProximityBonus = 2.0f;

    /** HP fraction (0-1) below which the most-injured ally is considered to need support.
     *  AllyProximityBonus only fires when AllyHP < this threshold.
     *  Default 0.9 means proximity bonus fires unless the ally is nearly full health. */
    UPROPERTY(EditAnywhere, Category = "AllyProximity")
    float AllyInjuryThreshold = 0.9f;

    /** Per-cm shaping reward for approaching the most-injured ally.
     *  FIX (Issue 3): Raised from 0.001 to 0.01 (= 1.0 per metre).
     *  At 0.001/cm the approach gradient was too weak to overcome the flat
     *  PositionReward, so the agent never learned to navigate toward allies. */
    UPROPERTY(EditAnywhere, Category = "AllyProximity")
    float AllyApproachReward = 0.05f;


    //========== Capture Properties =============

    /** Support: +10 per capture (base CaptureReward=100 × 0.10) */
    UPROPERTY(EditAnywhere, Category = "Capture")
    float CaptureRewardScale = 0.10f;

    /** Support: -15 per loss (base LossCaptureReward=-100 × 0.15) */
    UPROPERTY(EditAnywhere, Category = "Capture")
    float LossCaptureRewardScale = 0.15f;


    //========== Heal Properties =============

    /** Per-tick reward when actively healing an ally */
    UPROPERTY(EditAnywhere, Category = "Heal")
    float HealTickReward = 0.5f;

    //========== Positioning Properties =============

    /** Per-step bonus for being farther from the nearest visible enemy than the nearest ally.
     *  Only fires when the enemy is within RearGuardMaxEnemyDist — prevents rewarding far-corner hiding. */
    UPROPERTY(EditAnywhere, Category = "Positioning")
    float RearGuardBonus = 0.3f;

    /** Maximum distance to nearest visible enemy (cm) for RearGuardBonus to fire.
     *  Enemies beyond this range don't count as "threatening the backline". Default 3000cm = 30m. */
    UPROPERTY(EditAnywhere, Category = "Positioning")
    float RearGuardMaxEnemyDist = 3000.0f;


    //========== Movement Properties =============

    UPROPERTY(EditAnywhere, Category = "Movement")
    float PenaltyPerMeterScale = 1.5f;
};


/**
 * 에이전트의 성향 정의
 * 보상 스칼라화(Scalarization) 과정에서 어떤 목표를 우선할지 결정하는 가중치입니다.
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FAgentPersonality
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Personality")
    float WinWeight;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Personality")
    float SurvivalWeight;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Personality")
    float ObjectiveWeight;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Personality")
    float EfficiencyWeight;

    FAgentPersonality()
        : WinWeight(1.0f)
        , SurvivalWeight(0.5f)
        , ObjectiveWeight(1.5f)
        , EfficiencyWeight(0.2f)
    {}
};




/**
 * Multi-Objective Reward Structure
 * Value Network가 예측하는 벡터 형태의 보상입니다.
 */
USTRUCT(BlueprintType)
struct GAMEAI_PROJECT_API FCompositeReward
{
    GENERATED_BODY()

    /** 승리 확률 (0.0 ~ 1.0) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float WinProb;

    /** 체력 변화량 예측 (정규화된 값, 예: -0.1은 10% 손실) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float HealthDelta;

    /** 목표 달성도 점수 (거리, 점령 상태 등) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float ObjectiveScore;

    FCompositeReward()
        : WinProb(0.0f)
        , HealthDelta(0.0f)
        , ObjectiveScore(0.0f)
    {}

    /**
     * 벡터 보상을 단일 스칼라 값(Value)으로 변환합니다.
     * 에이전트의 현재 성향(Personality)에 따라 가중치가 달라집니다.
     */
    float Scalarize(const FAgentPersonality& Personality) const
    {
        // 체력 손실은 일반적으로 -1 ~ 1 사이로 클리핑하여 과도한 회피 방지
        float ClampedHealth = FMath::Clamp(HealthDelta, -1.0f, 1.0f);

        return (Personality.WinWeight * WinProb) +
               (Personality.SurvivalWeight * ClampedHealth) +
               (Personality.ObjectiveWeight * ObjectiveScore);
    }
};



/**
 * 에디터에서 보상 가중치와 임계값을 설정할 수 있는 데이터 에셋
 */
UCLASS(BlueprintType)
class GAMEAI_PROJECT_API URewardSettingsDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    // ==================== Common Base Rewards ====================
    UPROPERTY(EditAnywhere, Category = "Rewards|Common")
    float TimePenalty = 0.001f;

    UPROPERTY(EditAnywhere, Category = "Rewards|Common")
    float MatchWinReward = 50.0f;

    UPROPERTY(EditAnywhere, Category = "Rewards|Common")
    float KillReward = 10.0f;

    UPROPERTY(EditAnywhere, Category = "Rewards|Common")
    float DeathPenaltyReward = 100.0f;

    /** Terminal reward for losing the match (applied as sparse reward) */
    UPROPERTY(EditAnywhere, Category = "Rewards|Common")
    float MatchLossReward = -50.0f;

    /** Fraction of team average reward mixed into individual reward.
     *  FinalReward = (1-α)*Individual + α*TeamAvg. Uses 1-step lag to avoid ordering issues. */
    UPROPERTY(EditAnywhere, Category = "Rewards|Common")
    float TeamRewardMixingRatio = 0.2f;

    /** Fraction of KillReward awarded for an assist (scaled by normalized damage contribution) */
    UPROPERTY(EditAnywhere, Category = "Rewards|Common")
    float AssistRewardScale = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Rewards|Common")
    float SurvivalReward = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Rewards|Common")
    float TeamWipePenalty = 50.0f;

    UPROPERTY(EditAnywhere, Category = "Rewards|Common")
    float CaptureReward = 100.0f;

    UPROPERTY(EditAnywhere, Category = "Rewards|Common")
    float LossCaptureReward = -100.0f;

    UPROPERTY(EditAnywhere, Category = "Rewards|Common")
    float PenaltyPerMeter = -0.01f;

    // ==================== Tunable Thresholds ====================

    /** HP fraction below which CalculateSurvivalReward fires */
    UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
    float SurvivalHPThreshold = 0.3f;

    /** Assault: minimum health loss (normalized) to apply the per-step health penalty.
     *  FIX: Lowered from 0.5 to 0.05 — at 0.5 the penalty almost never fired (only one-shots). */
    UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
    float AssaultHealthLossThreshold = 0.05f;

    /** Assault: movement below this distance (cm) triggers idle penalty when not in zone */
    UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
    float AssaultIdleMovementThreshold = 50.0f;

    UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
    float AssaultCapturedZoneDecaySteps = 15.0f;

    /** Defend: movement below this distance (cm) awards the stationary position bonus */
    UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
    float DefendStationaryThreshold = 200.0f;

    /** Support: minimum movement (cm) for position reward (lower bound) */
    UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
    float SupportMinMoveThreshold = 100.0f;

    /** Support: maximum movement (cm) for position reward (upper bound) */
    UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
    float SupportMaxMoveThreshold = 500.0f;

    /** Support: maximum distance (cm) from the most-injured ally to trigger AllyProximityBonus */
    UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
    float SupportAllyProximityThreshold = 1500.0f;

    /** Defend: health fraction above which the health bonus applies */
    UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
    float DefendHealthThreshold = 0.7f;

    /** Support: health fraction above which the health bonus applies */
    UPROPERTY(EditAnywhere, Category = "Rewards|Thresholds")
    float SupportHealthThreshold = 0.8f;

    // ==================== Reward Normalization & Clamping ====================

    /** Global reward scale applied to all step rewards before clamping.
     *  Reduces reward magnitude so the value function can fit discounted returns.
     *  Raised from 0.01 to 0.05 so dense signals are stronger (baseline 0.03→0.0015). */
    UPROPERTY(EditAnywhere, Category = "Rewards|Clamp")
    float GlobalRewardScale = 0.1f;

    /** Scale applied to sparse rewards (kills, deaths, captures) BEFORE they're added to dense step reward.
     *  Reduces the magnitude gap between dense (~1.0) and sparse (~20-100) rewards so the
     *  value function can fit both. Applied inside DrainSparseReward(). */
    UPROPERTY(EditAnywhere, Category = "Rewards|Clamp")
    float SparseRewardScale = 0.2f;

    /** Minimum per-step reward (after scaling) */
    UPROPERTY(EditAnywhere, Category = "Rewards|Clamp")
    float StepRewardClampMin = -2.0f;

    /** Maximum per-step reward (after scaling) */
    UPROPERTY(EditAnywhere, Category = "Rewards|Clamp")
    float StepRewardClampMax = 2.0f;

    // ==================== Capture Loss Cooldown (B4) ====================

    /** Minimum seconds between loss penalties for the same capture point.
     *  Prevents flip-flop scenarios from flooding sparse negative rewards. */
    UPROPERTY(EditAnywhere, Category = "Rewards|Capture")
    float CaptureLossCooldownSeconds = 10.0f;

    // ==================== Strategy Baselines (B1/B2) ====================

    /** Unconditional per-step baseline for Assault so random policies don't diverge to -inf.
     *  Reduced from 0.02: zone control reward now provides the primary per-step income
     *  once bases are held, so this only serves as a safety floor for untrained agents. */
    UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
    float AssaultBaselineReward = 0.01f;

    /** Unconditional per-step baseline for Defend.
     *  Reduced from 0.03 for the same reason — zone control reward is the dominant signal. */
    UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
    float DefendBaselineReward = 0.01f;

    /** Unconditional per-step baseline for Support. Same purpose as DefendBaselineReward. */
    UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
    float SupportBaselineReward = 0.01f;

    // ==================== Zone Control Reward ====================

    /** Per-step reward = ZoneControlRewardPerBase × Scale × (FriendlyBases - EnemyBases).
     *  Creates a continuous dense signal aligned with the domination objective:
     *  positive when the team holds more bases, negative when the enemy dominates.
     *  The negative case discourages passive corner-hiding by penalising inaction
     *  while the enemy controls the map. */
    UPROPERTY(EditAnywhere, Category = "Rewards|ZoneControl")
    float ZoneControlRewardPerBase = 0.2f;

    /** Assault scale: smaller because capture events and approach rewards are the
     *  primary gradient; zone control is a secondary alignment signal. */
    UPROPERTY(EditAnywhere, Category = "Rewards|ZoneControl")
    float ZoneControlAssaultScale = 0.1f;

    /** Defend scale: larger because retaining friendly zones is the core objective. */
    UPROPERTY(EditAnywhere, Category = "Rewards|ZoneControl")
    float ZoneControlDefendScale = 1.5f;

    /** Support scale: neutral — zone state matters but is not the primary role objective. */
    UPROPERTY(EditAnywhere, Category = "Rewards|ZoneControl")
    float ZoneControlSupportScale = 1.0f;

    // ==================== Isolation Mode (last survivor) ====================

    /** Multiplier on the objective approach reward when the agent is the last survivor
     *  (all allies dead). Creates a stronger directional gradient toward capture points
     *  to prevent corner-hiding. Applied to Assault and Support; Defend uses it for
     *  the zone approach reward. Default 5× means ~0.05/cm instead of ~0.01/cm. */
    UPROPERTY(EditAnywhere, Category = "Rewards|Isolation")
    float IsolationApproachMultiplier = 5.0f;

    /** Number of consecutive steps all allies must be dead before isolation mode activates.
     *  Prevents false positives during normal respawn windows (typically 3-8 seconds).
     *  At 10 steps/s a value of 30 ≈ 3 seconds of sustained isolation. */
    UPROPERTY(EditAnywhere, Category = "Rewards|Isolation")
    int32 IsolationDebounceSteps = 30;

    // ==================== Per-Strategy Settings ====================
    UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
    FAssaultRewardSettings AssaultReward;

    UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
    FDefendRewardSettings DefendReward;

    UPROPERTY(EditAnywhere, Category = "Rewards|Strategy")
    FSupportRewardSettings SupportReward;
};


/**
 * Per-step reward breakdown for debug logging.
 * Plain C++ struct — not reflected via UE4 macros.
 */
struct FRewardBreakdown
{
    float StrategyReward       = 0.0f;
    float HealthComponent      = 0.0f;
    float PositionComponent    = 0.0f;
    float ObjectiveComponent   = 0.0f;
    float DeathPenaltyComponent = 0.0f;
    float TimePenaltyComponent = 0.0f;
    float Total                = 0.0f;
};



