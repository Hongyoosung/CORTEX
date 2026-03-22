
# CORTEX / MOC v10.2 - Session Memory

## Project

* **Environment:** UE5.6, C++17, Windows
* **Working Directory:** `Source/GameAI_Project`
* **Branch:** `feature-GAS`

## Current Architecture (Inference Mode)

* **Pawn:** `BP_Agent` (ADECharacter) — Contains `UDEScholaAgent` (UInferenceComponent subclass) and `UDEEQSExecutor`.
* **AIC:** `BP_AIC` (ADEAIController) — Possesses pawn, runs `BT_DEAgent` + `BB_DEAgent`, and handles AIPerception.
* **Movement Flow:** Schola Think/Act tick → `DETacticalParameterActuator::TakeAction()` → `ADECharacter::PerformTacticalAction()` → writes 6 float weights to BlackBoard (BB) → `BTTask_DEMoveToEQSLocation` reads BB → async EQS → `AIController->MoveTo()`.
* **Combat:** `BTTask_DEAttackAbility` (GAS), `BTDecorator_DEHasEnemyTarget`, and perception via `ADEAIController::OnPerceptionUpdated`.

## Schola Inference Cycle

* `UInferenceComponent` registers `FThinkTickFunction` and `FActTickFunction` (runs every frame).
* **Think():** If `Brain->IsDecisionStep()` → `RequestDecision()` → async ONNX future.
* **Act():** If `Brain->IsActionStep()` → `ResolveDecision()` **BLOCKS game thread** up to 30s → `DistributeActions()` → `TakeAction()`.
* `Brain->IncrementStep()` occurs after each Act.
* `DecisionRequestFrequency = 5` (default): ONNX called every 5 ticks.
* `bTakeActionBetweenDecisions = true` (default): Cached action is repeated between decision steps.
* **Error Handling:** If the Brain errors out, `Agent->SetStatus(Stopped)` and the cycle stops permanently.

---

## Inference Movement Bug — UNRESOLVED (2026-03-10)

### Symptoms

* Agent moves once to the first EQS target, then stops permanently.
* Behavior Tree (BT) debugger shows `BTTask_DEMoveToEQSLocation` still **InProgress** (task never aborts or fails).
* EQS weights in BB do not change after the first movement.
* `PerformTacticalAction` continues running (Schola cycle is alive), confirming BB weights are being written.

### Diagnosis

`PerformTacticalAction` only writes to BB and returns; it does **not** call `MoveTo` directly. All movement is driven by the BT task, which must continuously re-query EQS to keep the agent moving.

### Attempts at Resolution

1. **Attempt 1 — OnMoveCompleted callback chain (FAILED):** Task was designed to stay `InProgress` and call `IssueEQSQuery()` from `OnMoveCompleted`.
* *Result:* Agent moved once and stopped.
* *Root Cause:* `PFC->OnRequestFinished` callback chain has timing edge cases. If it fires synchronously or with a mismatched ID, the chain breaks.


2. **Attempt 2 — TickTask + PFC status polling (CURRENT):** Replaced callback chain with `bNotifyTick = true` and `TickTask` override.
* `TickTask` checks if `PFC->GetStatus() == Moving` and validates `MoveRequestID`.
* If not moving and no EQS is pending, it triggers `IssueEQSQuery()` with latest BB weights.
* Removes callback race conditions. Logs `[DEMoveToEQS] Query #X` every 50 queries.



---

## GAS Refactor (2026-03-09)

Migrated from custom ability/health system to UE5 Gameplay Ability System.

### Key Classes

* `DEAttributeSet`: Health, MaxHealth, Armor, and Meta-attributes (Damage/Healing).
* `DEGameplayAbility`: Base GA with dynamic tag-based cooldowns.
* `DEGA_Attack`: Projectile attack with ammo and cooldown logic.
* `DEGA_Heal`: Range-based ally heal with Niagara beam effects.
* `DEGameplayTags`: Native tags (e.g., `State.Dead`, `Ability.Attack`, `Cooldown.Attack`).

### BT Tasks

* `BTTask_DEAttackAbility`: Triggers `Ability.Attack`.
* `BTTask_DEHealAbility`: Triggers `Ability.Heal`.
* `BTTask_DEMoveToEQSLocation`: Handles inference-based movement.

---

## Observation Space

* **Total:** 54-dimensions `[base(48) | team_composition(3) | strategy_one_hot(3)]`
* **Base 48:** Self pos/vel/health (7), 4 Ally pos/health (12), 5 Enemy pos/vis (20), 5 Control Point states (5), Env origin offset (4).
* **Composition:** `[num_assault/5, num_defend/5, num_support/5]`
* **Strategy:** One-hot encoded `[Assault, Defend, Support]`.

## Known Issues

* `AScholaEnvironment` may spawn `ADETrainer` even in inference mode, potentially conflicting with `BP_AIC`.
* `SynchronousBrain::ResolveDecision()` blocks the game thread for up to 30s per ONNX call.

**Current Date:** 2026-03-10

---
