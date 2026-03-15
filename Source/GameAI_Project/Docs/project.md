---
title: "Dynamic EQS: <br> RL 기반의 UE5 전략적  포지셔닝 최적화 시스템"
description: "RL 모델을 통한 EQS 가중치 동적 최적화 및 Schola와 Ray RLlib을 활용한 AWS 클라우드 기반 병렬 강화학습 파이프라인 구축"

weight: 1
translationKey: "project-1"
duration: "2025.08 ~ 2026.03"
team_size: "1명"
role: "메인 프로그래머"
github: "https://github.com/yoosunghong/GOBTv2.0"
math: true
---

---

## 개요 (Overview)

본 프로젝트는 Unreal Engine 5 환경에서 5 vs 5 팀 기반 거점 점령전을 위한 전략적 포지셔닝 최적화를 목표로 합니다.

각 에이전트는 전략별로 분리된 강화학습(RL) 정책 네트워크와 **통합 EQS(Environment Query System)** 를 결합하여, 실시간 상황에 최적화된 공간 이동 파라미터를 추론합니다.

특히, Schola 플러그인을 브릿지로 활용하여 AWS 클라우드 기반의 Ray RLlib 대규모 병렬 학습 환경을 구축했습니다. 이를 통해 수십 개의 언리얼 엔진 인스턴스로부터 데이터를 동시 수집하고 정책을 업데이트하는 고성능 학습 파이프라인을 구현했습니다.


{{< gif-grid urls="/gifs/project1/1.gif, /gifs/project1/3.gif" widths="50%, 50%" >}}

---

## 시스템 아키텍처 (System Architecture)


{{< img src="/images/project1/archi.png" 
        alt="" 
        class="max-w-full" 
        caption="Fig 1. 시스템 아키텍처 및 계층적 포지셔닝 워크플로우" >}}


---

## 기술 스택 (Tech Stack)

| Category | Technologies |
|---|---|
| **Game Engine** | Unreal Engine 5.6 (C++17) |
| **RL Framework** | Ray RLlib 2.7, PyTorch |
| **UE5-Python Bridge** | Schola Plugin (gRPC-based) |
| **Neural Network Inference** | ONNX Runtime via UE5 NNE (Neural Network Engine) |
| **Ability System** | UE5 Gameplay Ability System (GAS) — GameplayAbility, GameplayEffect, GameplayTag, GameplayCue |
| **Cloud & Infra** | AWS (EC2, EKS), Docker (Linux) |
| **Communication** | gRPC (Schola protocol) |
| **Monitoring** | TensorBoard |


---


## 주요 기능 (Key Features)


### 1. Dynamic-EQS Plugin

DynamicEQS는 Schola 플러그인을 기반으로 하는 **재사용 가능한 UE5 전용 RL 통합 레이어**입니다. 게임 프로젝트가 Schola의 저수준 gRPC/ONNX 처리를 직접 다루지 않고 EQS 특화 추상 클래스만 상속하면 됩니다.

#### 플러그인 계층 구조

```
Schola::UInferenceComponent       ← gRPC/ONNX Think/Act 사이클
    └── UDynamicEQSAgentComponent ← EQS 특화 래퍼 (AgentMode, ExternalParams)
            └── UDEScholaAgent    ← 전술 전략 커맨드 수용

Schola::UBoxActuator              ← Action Tensor → TakeAction() 변환
    └── UDynamicEQSActuatorBase   ← EQS 가중치 추출 책임 선언
            └── UDETacticalParameterActuator ← 7-dim → FDEEQSWeightParameters

Schola::UBoxObserver              ← 관찰 벡터 수집
    └── UDynamicEQSObserverBase   ← 관찰 공간 선언
            └── UDETacticalObserver ← 170-dim 엔티티 중심 관찰 구성
```

#### 런타임 데이터 흐름 (Training ↔ Inference 듀얼 모드)

```mermaid
sequenceDiagram
    participant PY  as Python (RLlib PPO)
    participant SCH as Schola (gRPC / ONNX)
    participant ACT as UDETacticalParameterActuator
    participant CHR as ADECharacter
    participant EXE as UDEEQSExecutor
    participant EQS as UE5 EQS

    Note over PY,EQS: ① Think — 170-dim 관찰 수집 → 정책 추론
    SCH-->>PY: obs[170] via gRPC (Training) / ONNX (Inference)
    PY-->>SCH: action[7] — EQS 가중치 [-1, 1]

    Note over PY,EQS: ② Act — Action 수신 및 EQS 실행
    SCH->>ACT: TakeAction(FBoxPoint[7])
    ACT->>ACT: ActionToEQSWeights() · ValidateEQSWeights()
    ACT->>CHR: UpdateTacticalWeights() · PerformTacticalAction()

    alt Training 모드 (동기)
        CHR->>EXE: ExecuteSynchronousQuery(Weights)
        EXE->>EQS: RunEQSQuery (blocking, 48 samples)
        EQS-->>CHR: Best FVector → MoveTo()
    else Inference 모드 (비동기 + BT)
        CHR->>CHR: weights → Blackboard 기록
        EXE->>EQS: RunEQSQuery (async, BT TickTask)
        EQS-->>CHR: Best FVector → AIController::MoveTo()
    end
```

#### EQS 가중치 주입 (ApplyWeightsToRequest)

RL 정책의 `[-1, 1]` 출력을 `WeightScaleFactor(×2.0)`으로 스케일링해 UE5 EQS Named Parameter로 주입합니다.

```cpp
// DynamicEQSExecutor.cpp — 인덱스 기반 범용 주입 (플러그인 레이어)
for (int32 i = 0; i < WeightArray.Num(); ++i)
{
    FName ParamName = (WeightParamNames.IsValidIndex(i) && !WeightParamNames[i].IsNone())
        ? WeightParamNames[i]
        : *FString::Printf(TEXT("Weight%d"), i);
    Request.SetFloatParam(ParamName, WeightArray[i] * WeightScaleFactor);  // ×2.0
}
```

```cpp
// DEEQSExecutor.cpp — 7개 전술 파라미터 직접 주입 (게임 레이어)
Request.SetFloatParam(TEXT("EnemyObjectiveProximity"), Weights.EnemyObjectiveProximity * WeightScale);
Request.SetFloatParam(TEXT("AllyObjectiveProximity"),  Weights.AllyObjectiveProximity  * WeightScale);
Request.SetFloatParam(TEXT("CoverDensity"),            Weights.CoverDensity            * WeightScale);
Request.SetFloatParam(TEXT("EnemyVisibility"),         Weights.EnemyVisibility         * WeightScale);
Request.SetFloatParam(TEXT("AllyProximity"),           Weights.AllyProximity           * WeightScale);
Request.SetFloatParam(TEXT("CombatRange"),             Weights.CombatRange             * WeightScale);
Request.SetFloatParam(TEXT("AssignedBaseProximity"),   Weights.AssignedBaseProximity   * WeightScale);
```

#### FInstancedStruct를 활용한 게임 로직 디커플링

플러그인이 게임 전용 타입(`AssignedBaseIndex` 등)을 직접 멤버로 갖지 않도록, `FInstancedStruct`로 외부 파라미터를 불투명하게 보관합니다.

```cpp
// DynamicEQSAgentComponent.h — 플러그인은 FInstancedStruct만 알고 있음
UPROPERTY(BlueprintReadWrite)
FInstancedStruct ExternalParams;  // 어떤 USTRUCT도 저장 가능

// DETacticalParameterActuator.cpp — 사용 시점에 게임 타입으로 캐스팅
const FDEAgentExternalContext* Ctx =
    AgentComponent->ExternalParams.GetPtr<FDEAgentExternalContext>();
if (Ctx)
    Weights.AssignedBaseProximity = ComputeBaseProximityWeight(Ctx->AssignedBaseIndex);
```

이 패턴 덕분에 플러그인은 게임 헤더를 전혀 포함하지 않으며, 다른 프로젝트에서 그대로 재사용할 수 있습니다.

---


### 2. 관측 공간 및 전략 조건부 보상 설계 (Strategy-Conditioned Reward Shaping)

**관측 공간**

`FDEObservationV2::ToFlatArray()`가 생성하는 170-dim 엔티티 중심(Entity-Centric) 벡터입니다. 아군·적·거점을 고정 크기 슬롯 토큰으로 인코딩하고, 패딩 마스크를 별도로 제공해 Python MultiheadAttention이 유효 엔티티만 처리하도록 합니다.

```
[Index]    [Dim]  [토큰]         [정규화]
[0 : 3]     3    자신 위치       / (7500, 7500, 1000)
[3 : 6]     3    자신 속도       / (600, 600, 600)
[6 : 7]     1    자신 체력       raw [0,1]
[7 :47]    40    아군 토큰 8×5   rel_pos/8000(3) + health(1) + alive(1)
[47:87]    40    적 토큰   8×5   rel_pos/8000(3) + visible(1) + confidence(1)
[87:143]   56    거점 토큰 8×7   rel_pos/15000(2) + rel_z/1000(1)
                                 + ownership(1) + cap_progress(1)
                                 + is_assigned(1) + strategic_val(1)
[143:151]   8    아군 마스크     0=유효, 1=패딩
[151:159]   8    적 마스크
[159:167]   8    거점 마스크
[167:170]   3    전략 원-핫      [assault, defend, support]
════════  170    TOTAL
```

> **마스크 처리:** Python 정책의 `_safe_mask()`는 모든 슬롯이 패딩일 때 슬롯 0을 강제 언마스크하여 MultiheadAttention의 NaN을 방지합니다. 마스크 임계값은 `> 0.5` (float 비교)로, `0.0=유효 / 1.0=패딩` 의미론을 보존합니다.

<br>

**보상 구조 개요**

`DERewardSubsystem`은 **Phase 5 협력적 거점 점령** 보상 구조를 사용합니다. 에이전트가 단순히 같은 거점에 뭉치는 대신, 팀 전체가 서로 다른 거점을 분산 점령하도록 유도합니다.

| 보상 항목 | 값 | 조건 |
|---|---|---|
| `BaseOccupationReward` | **+2.0**/step | 아군 혼자 중립 거점 근처 |
| `CoOccupationPenalty` | **−0.5**/step | 동일 거점에 2명 이상 |
| `BaseCaptureCreditReward` | **+5.0** | 거점 점령 완료 기여 |
| `UndefendedBasePenalty` | **−1.0**/step | 방어되지 않은 아군 거점 |
| `AssignedBaseReachReward` | **+1.0** | 배정 거점 최초 도달 |

최종 보상은 `Σ(ComponentReward × PersonalityWeight) + StrategyBonus`로 합산되며, C++ `UDERewardData`와 Python `REWARD_CONFIG`의 수치를 항상 동기화해야 합니다.



---

> **돌격: 거점의 확보**

목표는 적 거점 접근과 점령 완료입니다. 적 거점까지의 거리 감소분에 비례한 접근 보상을 매 스텝 부여하고, 거점 반경 내 진입 시 추가 존재 보너스를 부여합니다. 점령이 완료되면 즉시 `PostCaptureMomentumDuration` 스텝 동안 모멘텀 보너스가 활성화되어, 점령 후 제자리에 머무는 대신 다음 거점으로 계속 전진하도록 유도합니다.

```cpp
// DERewardCalculator.cpp — Assault 보상
float Reward = 0.f;
// 거점 반경 내 진입 보상
if (DistToEnemyObjective < ObjectiveRadius)
    Reward += RewardData->AssaultSettings.ObjectivePresenceBonus;
// 점령 직후 모멘텀 보너스 (PostCaptureMomentumDuration 스텝 유지)
if (MomentumStepsRemaining > 0)
{
    Reward += RewardData->AssaultSettings.PostCaptureMomentumBonus;
    --MomentumStepsRemaining;
}
// 거점 점령 완료 기여
if (Context.bCapturedBase)
{
    Reward += RewardData->BaseCaptureCreditReward;
    MomentumStepsRemaining = RewardData->AssaultSettings.PostCaptureMomentumDuration;
}
```

---

> **방어: 거점의 유지**

목표는 아군 거점 유지와 거점 내 적 격퇴입니다. 아군 거점 반경 내에 위치할 때 기본 존재 보상이 부여됩니다. 거점 내에서 적에게 데미지를 받으면 추가 내구도 보너스(`ZoneDurabilityBonus`)가 지급되어, 거점에서 물러나지 않고 버티는 행동을 강화합니다. 아군 거점이 없는 상황에서는 중립/적 거점 접근으로 목표가 전환됩니다.

```cpp
// DERewardCalculator.cpp — Defend 보상
float Reward = 0.f;
if (bInsideFriendlyBase)
{
    Reward += RewardData->DefendSettings.BasePresenceBonus;   // 거점 내 존재 보상
    // 거점 안에서 피격 시 버티기 강화
    if (Context.DamageTaken > 0.f)
        Reward += RewardData->DefendSettings.ZoneDurabilityBonus;
}
else if (!bHasFriendlyBase)
{
    // 아군 거점 없으면 중립/적 거점으로 목표 전환
    if (DistToNeutralObjective < ObjectiveRadius)
        Reward += RewardData->DefendSettings.NeutralObjectiveBonus;
}
// 방어되지 않은 아군 거점 패널티
if (bUndefendedFriendlyBase)
    Reward += RewardData->UndefendedBasePenalty;   // -1.0/step
```

---

> **지원: 아군의 유지**

목표는 체력이 낮은 아군을 추적하고 힐링하며 후방을 유지하는 것입니다. 매 스텝 부상 아군 탐색을 수행하되, 잦은 타겟 전환으로 인한 진동 행동을 막기 위해 5스텝 캐시를 적용합니다. 캐시된 아군이 현재 가장 낮은 체력이 아니더라도 5스텝이 지나기 전까지는 교체하지 않습니다. 아군 뒤편에 위치하면 후방 포지셔닝 보너스를 받으며, 아군이 부상 중인 상황에서 직접 킬을 시도하면 역할 이탈 패널티가 부과됩니다.

```cpp
// DERewardCalculator.cpp — Support 보상
// 5-스텝 캐시: 타겟 진동 방지
if (SupportTargetCacheSteps <= 0 || !CachedHealTarget || !CachedHealTarget->IsAlive())
{
    CachedHealTarget    = FindLowestHealthAlly();
    SupportTargetCacheSteps = 5;
}
--SupportTargetCacheSteps;

float Reward = 0.f;
// 힐링 누적량에 비례한 보상 (보상 서브시스템으로 환류)
Reward += Context.CumulativeHealAmount * RewardData->SupportSettings.HealRewardScale;
// 아군 뒤편 후방 포지셔닝 보너스
if (bBehindCachedAlly)
    Reward += RewardData->SupportSettings.RearPositioningBonus;
// 부상 아군 존재 시 킬 시도 → 역할 이탈 패널티
if (CachedHealTarget && Context.bKilledEnemy)
    Reward += RewardData->SupportSettings.RoleDivergencePenalty;  // 음수

---

**독립 정책 네트워크와 전략 균형 리플레이 버퍼**

공유 인코더 + 멀티헤드 구조에서는 Support 헤드가 학습 초기에 붕괴하는 그래디언트 간섭 문제가 반복됐습니다. 이를 해결하기 위해 전략별 완전 독립 단일 헤드 정책으로 전환했습니다.

```python
# phase1_policy_training_v10_2.py — 전략별 독립 정책 등록
config = config.multi_agent(
    policies={
        "assault_policy": PolicySpec(),   # 독립 인코더 + 단일 헤드
        "defend_policy":  PolicySpec(),
        "support_policy": PolicySpec(),
    },
    policy_mapping_fn=_strategy_policy_mapping_fn,  # 에이전트 전략 → 정책 라우팅
)
```

세 전략이 동시에 존재하는 학습 환경에서 한 전략 데이터가 과다 수집되어 편향이 생기는 것을 막기 위해, 전략별로 독립된 서브 버퍼를 두고 샘플링 시 33/33/33% 균등 분배를 강제하는 `StrategyBalancedReplayBuffer`를 구현했습니다.

```python
class StrategyBalancedReplayBuffer:
    def __init__(self, capacity: int = 100000):
        # 전략별(Assault/Defend/Support) 독립 서브 버퍼
        self.buffers = {
            strategy: deque(maxlen=capacity // 3)
            for strategy in range(3)
        }

    def sample(self, batch_size: int) -> List[Transition]:
        """각 전략 버퍼에서 균등하게 샘플링 (33/33/33%)."""
        samples_per_strategy = batch_size // 3
        batch = []
        for strategy in range(3):
            buffer = self.buffers[strategy]
            indices = np.random.choice(len(buffer), samples_per_strategy, replace=False)
            batch.extend([buffer[i] for i in indices])
        np.random.shuffle(batch)
        return batch
```

관측 공간에 아군의 전략 분포(팀 구성비)도 포함하여, 에이전트가 팀 내 전략 조합을 인식하고 협동 행동을 발견할 수 있도록 유도했습니다.

---



### 3. AWS 클라우드 상의 병렬 학습을 위한 컨테이너화 및 환경 매니징 시스템

대규모 병렬 강화학습을 안정적으로 구동하기 위해, Python 학습 환경 전체를 Linux Docker 컨테이너로 패키징하고 AWS EC2 위에서 여러 UE5 인스턴스와 동시에 연결되는 파이프라인을 구축했습니다.

#### 컨테이너화 전략

Python 학습 스크립트(Ray RLlib, Schola 등 의존성 포함)를 Linux 컨테이너 이미지로 빌드합니다. Windows 환경에서 Ray의 멀티프로세스 생성 방식(`spawn`/`fork`)이 충돌하던 문제를 Linux 컨테이너로 전환하면서 원천적으로 해결했습니다. 패키징된 UE5 빌드는 별도 Linux 인스턴스에서 실행되며 컨테이너와 gRPC로 통신합니다.

#### 동적 포트 라우팅

각 RLlib env-runner가 독립된 UE5 인스턴스에 연결되도록, 워커 인덱스 기반의 포트 자동 배정 로직을 구현했습니다.

```python
# de_env.py — 워커별 포트 자동 배정
def _resolve_port(self, **kwargs):
    """멀티 워커 RLlib 환경에서 포트를 자동으로 배정."""
    base_port = kwargs.get("base_port")
    if base_port is not None:
        from ray.rllib.evaluation.rollout_worker import get_global_worker
        worker = get_global_worker()
        worker_index = worker.worker_index if worker else 0
        return base_port + max(0, worker_index - 1)
    return base_port
```

RLlib이 여러 env-runner를 생성할 때, 각 워커는 `base_port + worker_index` 방식으로 고유한 포트를 할당받아 서로 다른 UE5 인스턴스에 독립적으로 연결됩니다.

#### 환경 변수 기반 오케스트레이션

학습 규모와 하이퍼파라미터를 소스 코드 수정 없이 Docker Compose 설정만으로 제어합니다.

```python
# phase1_policy_training_v10_2.py — 환경 변수로 학습 규모 동적 조절
PORT                  = 50051
NUM_UE5_ENVIRONMENTS  = int(os.environ.get('NUM_SCHOLA_ENVS', 4))
NUM_WORKERS           = int(os.environ.get('NUM_WORKERS', 0))
NUM_ITERATIONS        = int(os.environ.get('NUM_ITERATIONS', 100))
```

`NUM_SCHOLA_ENVS`와 `NUM_WORKERS`를 Docker Compose의 `environment` 블록에서 지정하면, 코드 변경 없이 UE5 인스턴스 수와 Ray 워커 수를 독립적으로 스케일 아웃할 수 있습니다. 이를 통해 하이퍼파라미터 스윕(Hyperparameter Sweep)도 Docker Compose 파일 수준에서 빠르게 실행할 수 있습니다.

---

### 4. GAS 기반 전투 능력 시스템 (Gameplay Ability System Integration)

전투 로직(공격·힐링)을 UE5 **Gameplay Ability System(GAS)** 으로 설계하여, AI 에이전트의 능력 실행·쿨다운·속성 관리를 표준화된 데이터 주도 파이프라인으로 통합했습니다.

#### 핵심 설계 원칙

**Gameplay Tag** 기반 조회(`TryActivateAbilitiesByTag`)로 Behavior Tree 태스크와 . 이를 통해 Behavior Tree 태스크가 구체적인 구현 클래스를 몰라도 능력을 트리거할 수 있어 능력 교체·확장이 코드 수정 없이 가능합니다.

#### 속성 관리: `UDEAttributeSet`

| 속성 | 종류 | 설명 |
| --- | --- | --- |
| `Health` / `MaxHealth` | Persistent (복제됨) | 에이전트 생존 상태 추적 |
| `Armor` | Persistent (복제됨) | 피해 감산 계수 (1포인트 = 1% 감소) |
| `Damage` / `Healing` | Meta (소모성) | GameplayEffect 적용 시 즉시 소비, 지속되지 않음 |

`PostGameplayEffectExecute()`에서 `Damage` 메타 속성을 소비해 아머 감산(`1 - Armor * 0.01`) → 무적 태그 확인 → Health 클램핑 → 사망 시 `State.Dead` 태그 부착의 파이프라인이 원자적으로 처리됩니다.

```cpp
// DEAttributeSet.cpp — 피해 처리 파이프라인
if (Data.EvaluatedData.Attribute == GetDamageAttribute())
{
    const float MitigationFactor = 1.0f - FMath::Clamp(GetArmor() * 0.01f, 0.0f, 0.9f);
    float FinalDamage = GetDamage() * MitigationFactor;

    // 무적 태그가 있으면 피해 무효
    if (SourceASC && SourceASC->HasMatchingGameplayTag(DEGameplayTags::State_Invulnerable))
        FinalDamage = 0.0f;

    const float NewHealth = FMath::Clamp(GetHealth() - FinalDamage, 0.0f, GetMaxHealth());
    SetHealth(NewHealth);

    // 사망 처리: State.Dead 태그 부착
    if (NewHealth <= 0.0f)
        AbilitySystemComponent->AddLooseGameplayTag(DEGameplayTags::State_Dead);

    SetDamage(0.0f); // 메타 속성 소비
}
```

#### 공격 능력: `UDEGA_Attack`

- **서버 전용 실행** (`NetExecutionPolicy::ServerOnly`): AI 전용 능력으로 클라이언트 예측 불필요
- 활성화 시 `State.Dead` 태그 보유 여부를 차단 조건으로 검사
- `FindNearestEnemy()`로 범위·시야 기반 자동 타겟 선택 후 프로젝타일 스폰
- `AIController->SetFocus()`로 조준 방향을 애니메이션 블루프린트에 전달
- 쿨다운은 `Cooldown.Attack` 태그 기반 `GE_AttackCooldown` 이펙트로 GAS 내에서 관리

```cpp
// DEGA_Attack.cpp — 능력 활성화 분기
void UDEGA_Attack::ActivateAbility(...)
{
    if (!CommitAbility(Handle, ActorInfo, ActivationInfo, &FailureTags))
    { EndAbility(...); return; }

    AActor* Target = TargetActor ? TargetActor : FindNearestEnemy();
    if (!IsTargetValid(Target))
    { EndAbility(...); return; }

    // AI 조준 방향 설정 → 애니메이션 에임오프셋 구동
    if (AController* Ctrl = Character->GetController())
        Ctrl->SetFocus(Target);

    FireAtTarget(Target, ActorInfo);
    EndAbility(...);
}
```

#### 힐링 능력: `UDEGA_Heal`

- `FindNearestInjuredAlly()`로 팀 내 최저 체력 아군을 자동 선택 (5스텝 캐시와 독립적으로 동작)
- `SetByCaller` 매그니튜드(`Data.Healing` 태그)로 힐량을 런타임에 동적 지정
- `CumulativeHealAmount` 누적값을 보상 서브시스템에 제공하여 지원 역할 밀도 보상으로 환류

```cpp
// DEGA_Heal.cpp — GameplayEffect를 통한 힐 적용
FGameplayEffectSpecHandle SpecHandle =
    AbilitySystemComponent->MakeOutgoingSpec(HealEffectClass, 1.0f, EffectContext);

SpecHandle.Data->SetSetByCallerMagnitude(DEGameplayTags::Data_Healing, HealAmount);
TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
```

#### BT 태스크 연동

`BTTask_DEAttackAbility` · `BTTask_DEHealAbility` 양쪽 모두 내부 구현이 아닌 **Gameplay Tag 조회**를 통해 능력을 활성화합니다. Behavior Tree는 능력 교체·파라미터 변경에 완전히 무관하며, 풀오토 공격은 `TickTask`에서 매 프레임 재활성화하는 방식으로 구현됩니다.



---

### 5. 듀얼 모드 아키텍처 (Dual-Mode Architecture)

모든 주요 컴포넌트는 단일 UE5 바이너리 내에서 **학습 모드(Training)** 와 **추론 모드(Inference)** 를 동시에 지원하도록 설계했습니다. 학습이 끝난 ONNX 모델을 별도의 빌드 없이 동일한 UE5 환경에서 즉시 실행하고 검증할 수 있습니다.

#### 핵심 설계: `UDEScholaAgent`

에이전트 컴포넌트 `UDEScholaAgent`가 두 모드를 하나의 인터페이스로 추상화합니다. `CurrentMode` 프로퍼티 하나로 행동 파이프라인 전체가 분기됩니다.

```cpp
// DEScholaAgent.h
UENUM(BlueprintType)
enum class EDEAgentMode : uint8
{
    Training    UMETA(DisplayName = "Training Mode (Python RLlib)"),
    Inference   UMETA(DisplayName = "Inference Mode (Local ONNX)")
};

UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class UDEScholaAgent : public UInferenceComponent
{
    // Blueprint에서 에디터 단에서 모드를 전환할 수 있음
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DE")
    EDEAgentMode CurrentMode = EDEAgentMode::Training;
    ...
};
```

#### 모드별 실행 분기: `PerformTacticalAction()`

EQS 가중치가 결정된 뒤, 실제 이동 명령을 어떻게 처리하는지가 두 모드의 핵심 차이입니다.

```cpp
// DECharacter.cpp — 모드에 따라 EQS 실행 경로가 분기됨
void ADECharacter::PerformTacticalAction()
{
    bool bIsTraining = ScholaAgent &&
                       ScholaAgent->CurrentMode == EDEAgentMode::Training;

    if (BB && !bIsTraining)
    {
        // 추론 모드: 가중치를 Blackboard에 기록하고, Behavior Tree가 EQS를 실행
        BB->SetValueAsFloat(TEXT("Weight_EnemyObj"),  CurrentEQSWeights.EnemyObjectiveProximity);
        BB->SetValueAsFloat(TEXT("Weight_AllyObj"),   CurrentEQSWeights.AllyObjectiveProximity);
        BB->SetValueAsFloat(TEXT("Weight_Cover"),     CurrentEQSWeights.CoverDensity);
        BB->SetValueAsFloat(TEXT("Weight_EnemyVis"),  CurrentEQSWeights.EnemyVisibility);
        BB->SetValueAsFloat(TEXT("Weight_AllyProx"),  CurrentEQSWeights.AllyProximity);
        BB->SetValueAsFloat(TEXT("Weight_Range"),     CurrentEQSWeights.CombatRange);
        return;
    }

    // 학습 모드: EQS를 동기적으로 직접 실행하고 즉시 이동 명령 발행
    AICtrl->StopMovement();
    TOptional<FVector> Result = EQSExecutor->ExecuteSynchronousQuery(CurrentEQSWeights);
    ...
}
```

<br>

**학습 모드, 추론 모드 비교 테이블**

| 구분 | 학습 모드 | 추론 모드 |
|---|---|---|
| **정책 소스** | Python RLlib (gRPC) | 로컬 ONNX 모델 (UE5 NNE) |
| **EQS 실행** | C++에서 동기 직접 실행 | Blackboard → Behavior Tree 위임 |
| **Behavior Tree (이동)** | 사용 안 함 (동기 경로로 대체) | `BTTask_DEMoveToEQSLocation` |
| **Behavior Tree (전투)** | 사용 (인식 → Blackboard → 공격) | 사용 (동일) |
| **에피소드 관리** | `ADEScholaEnvironment`가 제어 | 불필요 (게임 루프로 동작) |
| **환경 리셋** | Schola 프로토콜 기반 | 없음 |

<br>

#### 설계 의도: 학습 모드에서 BT 이동 경로를 제외한 이유

추론 모드에서 BT가 담당하는 이동 경로(`BTTask_DEMoveToEQSLocation`)는 학습 모드에서 의도적으로 사용하지 않습니다. Schola의 스텝 루프는 **"액션 수신 → 실행 → 관측+보상 수집"** 이 단일 틱 내에서 결정론적으로 완결되어야 합니다. BT의 EQS 태스크는 비동기로 동작하기 때문에, 액션을 받은 직후 수집되는 관측값이 아직 이동이 반영되지 않은 상태(`s'`가 실제로는 `s`인 상황)가 될 수 있습니다. 이는 정책이 학습하는 전이 튜플 `(s, a, s', r)`을 오염시켜 보상 신호의 정확성을 떨어뜨립니다.

반면 동기 직접 실행 경로는 EQS 쿼리와 이동 명령이 같은 틱에 완료되므로 전이 튜플이 항상 일관됩니다. `BTTask_DEMoveToEQSLocation`은 기능적으로 동일한 EQS 쿼리를 비동기 래퍼로 감싼 것에 불과하므로, 학습 품질 손실 없이 동기 경로로 대체할 수 있습니다.

전투 BT(인식 → Blackboard → `BTTask_DEAttackAbility`)는 Schola 스텝 클럭과 독립적인 이벤트 구동 방식이므로 두 모드 모두에서 동일하게 동작합니다.

<br>




---

## 기술적 난제 및 해결 전략 (Problem Solving)


### Problem 1: UE5 환경과 Rllib 환경의 통신 표준 프레임워크의 필요성

기존 UE5에서 제공하는 강화학습 프레임워크인 **Learning Agent**는 UE5 로컬에서만 동작하기에 Python Ray Rllib의 분산 강화학습이라는 이점을 활용할 수 없었습니다.


#### Goal
UE5와 Python(Ray RLlib) 간의 고성능 강화학습 파이프라인을 구축하기 위해 안정적인 통신 수단과 표준 포멧이 포함된 프레임워크의 확보.


#### Solution: Schola Plugin의 도입

AMD의 오픈소스 라이브러리 Schola를 프레임워크로 채택하였으며 아래와 같은 이점을 얻었습니다.

* **표준 포멧 및 환경 구성**: Schola는 UE5에서 RL을 위한 관측(UBoxObserver)과 액션(UBoxActuator) 인터페이스를 제공하며 에이전트를 위한 전용 컨트롤러(AAbstractTrainer)와 Rllib와의 주요 통신 인터페이스(AStaticScholaEnvironment)를 통해 직관적인 RL 환경 구성을 가능하게 합니다.


* **래핑 및 gRPC 통신 브릿지:** Schola는 UE5 내의 데이터를 Python의 gym.Env 형태로 래핑하고 gRPC 프로토콜 기반의 직렬화를 통해 저지연으로 Rllib 환경과 통신하는 API를 제공합니다.


#### Result
오픈소스 라이브러리 Schola 프레임워크에서 제공하는 인터페이스를 통해 직접적인 래퍼 환경을 구축하지 않고도 UE5와 Python Ray Rllib의 통신을 성공적으로 완료하였습니다. 이를 통해 개발 효율을 대폭 향상시켰습니다.


```python
from ray.rllib.env.multi_agent_env import MultiAgentEnv
RLLIB_AVAILABLE = True

from schola.core.env import ScholaEnv, AutoResetType
from schola.core.unreal_connections.editor_connection import UnrealEditorConnection
SCHOLA_AVAILABLE = True
```

---

### Problem 2: 멀티 에이전트 에피소드 경계에서의 학습 프리징(정지) 현상

{{< img src="/images/project1/problem2_ko.png" 
        alt="" 
        class="max-w-3xl" 
        caption="그림 5. 프리징 현상의 원인과 해결" >}}

학습 도중 에피소드가 끝나는 경계 시점에서 시스템 전체가 멈추는 현상이 반복적으로 발생했습니다. 생존한 에이전트는 새로운 가중치 업데이트를 받지 못하고 정지했고, 이미 사망한 에이전트는 '사망 → 자동 부활 → 즉사' 사이클을 무한 반복하고 있었습니다.

#### Goal
에피소드 경계에서의 프리징 현상 해결 및 안정적인 멀티 에이전트 에피소드 종료 구현.

#### 근본 원인 분석: 두 종료 시스템의 충돌

Schola 측(`AutoResetType::SAME_STEP`)과 Python 측(RLlib) 사이에 에피소드 종료 신호가 서로 모순되는 상태였습니다.

- **Schola 측**: 에이전트가 사망하면 `SAME_STEP` 정책에 의해 즉시 자동 리셋을 트리거했습니다.
- **Python 측**: RLlib은 혼합 궤적(서로 다른 에피소드의 데이터가 한 배치에 섞이는 것)이 생기지 않도록, 모든 에이전트의 종료 신호(`done`)를 억제하고 있었습니다.

결과적으로 사망한 에이전트는 Schola가 부활시키자마자 다시 사망하는 무한 루프에 빠지고, 그 루프가 Schola의 스텝 예산(step budget) 전체를 소비해버렸습니다. 생존한 에이전트들은 스텝 버짓이 고갈된 Schola의 멀티에이전트 동기화 장벽(step barrier)에 막혀 영원히 다음 액션을 받지 못하는 상태가 되었습니다.


#### Solution

**1. 사망을 에피소드 종료 조건에서 제외 (`ComputeStatus`)**

`ADETrainer::ComputeStatus()`에서, 에이전트 사망 시 `Running`을 반환하도록 변경했습니다. 이로써 Schola의 자동 리셋 루프가 차단됩니다.

```cpp
// DETrainer.cpp — ComputeStatus()
EAgentTrainingStatus ADETrainer::ComputeStatus()
{
    // MaxEpisodeSteps 도달 시에만 에피소드를 종료
    if (CurrentEpisodeSteps >= MaxEpisodeSteps)
        return EAgentTrainingStatus::Truncated;

    // 에이전트 사망은 에피소드 종료가 아님.
    // DEMatchManager가 팀 단위로 리스폰을 처리할 때까지 Running 유지.
    if (!ControlledCharacter->IsAlive_Implementation())
        return EAgentTrainingStatus::Running;

    // 매치 종료(시간 초과, 점수 도달)도 종료 조건
    if (bMatchOver && !bHasNewReward)
        return EAgentTrainingStatus::Truncated;

    return EAgentTrainingStatus::Running;
}
```

**2. 사망한 에이전트의 스텝 배리어 참여 (`Tick` dead-agent drain)**

Schola의 멀티에이전트 스텝 배리어는 **모든** 에이전트가 액션을 소비해야 다음 스텝으로 진행됩니다. 사망한 에이전트가 액션 소비를 건너뛰면 배리어가 영원히 해제되지 않습니다.

사망한 에이전트도 `Tick()`에서 Schola가 보내는 액션을 소비(`ConsumeNewWeights`)하고, 그 횟수를 `CurrentEpisodeSteps`에 반영하도록 했습니다.

```cpp
// DETrainer.cpp — Tick() 사망 에이전트 처리
if (!ControlledCharacter->IsAlive_Implementation())
{
    // 사망 중에도 Schola의 액션을 소비하여 스텝 배리어를 해제
    if (ControlledCharacter->ConsumeNewWeights())
    {
        bHasNewReward = true;
        // 사망 에이전트도 MaxEpisodeSteps를 향해 스텝 카운트 진행.
        // 이 처리가 없으면 사망 에이전트는 MaxEpisodeSteps에 도달하지 못하고,
        // ComputeStatus()는 영원히 Running을 반환한다.
        CurrentEpisodeSteps++;
    }
    return;
}
```

#### Result
모든 에이전트(생존/사망 무관)가 `MaxEpisodeSteps`에서 동시에 에피소드를 종료하게 되었습니다. RLlib는 에피소드 경계가 깔끔하게 정렬된 단일 궤적 배치를 수신하여, 혼합 궤적 없이 안정적인 PPO 업데이트를 수행합니다.



---


### Problem 3: 학습 환경 병렬화에 따른 환경 불안정 문제

Windows 환경에서 Ray의 멀티 워커 아키텍처를 구동할 때 두 가지 문제가 발생했습니다. (1) 가중치 동기화 단계에서 Ray Learner 액터가 멈추는 현상, (2) 단일 UE5 인스턴스에 모든 워커가 연결을 시도하여 처리량이 병목되는 문제였습니다.

#### Goal
UE5와의 안정적인 통신을 유지하면서, OS 의존성 없이 수평 확장 가능한 멀티 워커 학습 파이프라인 구축.

#### Solution

**Docker 컨테이너화**: Python 학습 스크립트와 Ray RLlib 의존성 전체를 Linux Docker 이미지로 패키징했습니다. Windows의 `spawn`/`fork` 프로세스 생성 방식이 Ray와 충돌하던 문제를 컨테이너 레이어에서 원천적으로 차단했습니다.

**gRPC 포트 라우팅 최적화**: Schola의 연결 초기화 과정을 커스텀하여, 각 RLlib env-runner가 `base_port + worker_index` 공식으로 고유한 포트를 계산해 서로 다른 UE5 인스턴스에 접속하도록 했습니다.

```python
# de_env.py — Schola 연결 초기화
connection = UnrealEditorConnection(url=host, port=port)
self.schola_env = ScholaEnv(
    connection,
    auto_reset_type=AutoResetType.SAME_STEP
)
```

각 워커가 계산한 `port`는 `_resolve_port()`에서 `base_port + worker_index`로 결정됩니다. 결과적으로 N개의 워커가 각자 독립된 UE5 인스턴스와 1:1로 통신하는 구조가 완성됩니다.

**환경 변수 기반 오케스트레이션**: `NUM_SCHOLA_ENVS`, `NUM_WORKERS`, `NUM_ITERATIONS` 등을 환경 변수로 관리하여, 소스 코드 수정 없이 Docker Compose 설정만으로 학습 규모와 하이퍼파라미터를 동적으로 제어합니다.

#### Result
UE5 인스턴스와 Python 워커를 독립적으로 수평 확장할 수 있는 구조가 완성되었습니다. Docker 기반 파이프라인 도입으로 로컬 환경 의존성을 완전히 제거했으며, Docker Compose 파일 교체만으로 신속한 **하이퍼파라미터 스윕(Hyperparameter Sweep)**을 실행할 수 있게 되었습니다.




---

## 결과 (Results)
