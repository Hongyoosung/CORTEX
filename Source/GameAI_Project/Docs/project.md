---
title: "Dynamic EQS: <br> RL 모델을 통한 EQS 가중치 동적 최적화 시스템"
description: "Schola와 Ray RLlib을 활용한 AWS 클라우드 기반 멀티 에이전트 병렬 강화학습 파이프라인 구축"

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

본 프로젝트는 Unreal Engine 5 환경에서 강화학습 모델을 활용한 EQS(Environment Query system) 가중치
 업데이트를 지원하는 플러그인의 개발을 목표로 합니다.

본 프로젝트에서는 팀 기반 거점 점령전에서의 전략적 포지셔닝 최적화 환경을 대상으로 해당 플러그인을 활용하였습니다.

---

## 학습 환경 (Training Environment)

**5 vs 5 팀 기반 거점 점령전**으로, 맵 상에 5개의 거점(Capture Point)이 배치됩니다.

- **승리 조건:** 더 많은 거점을 점령하고 유지하는 팀이 승리합니다.
- **부활 규칙:** 개별 사망이 아닌 **팀 전멸(Team Wipe)** 시 해당 팀 전체가 리스폰됩니다. 부활 대기 시간(`RespawnDelay`) 동안 대기 후 팀 스폰 지점 근처에 동시 재배치됩니다.
- **전략 역할:** 각 에이전트는 매 에피소드마다 Squad Commander로부터 **Assault / Defend / Support** 전략을 부여받으며, 해당 전략에 최적화된 포지셔닝 행동을 학습합니다.
- **에피소드 길이:** 고정 스텝 수 기반으로 종료됩니다.
- **셀프플레이:** 양 팀이 동일한 정책을 공유하며 서로의 전략에 반응하는 Self-Play 방식으로 학습합니다.

각 에이전트는 강화학습 정책 네트워크를 통해 실시간 상황에 따라 공간 이동 파라미터를 추론하여 현재 상황에서의 최적 위치를 결정할 수 있습니다.

Schola 플러그인을 통해 Unreal Engine 5의 강화학습 환경과 외부 스크립트(Ray Rllib)와의 gRPC 기반 브릿지를 구성하였으며 Schola Layer 위에 EQS 가중치를 설정하고 정책 네트워크 출력과 연결하는 Dynamic EQS를 플러그인 형태로 구현하였습니다.

훈련은 초기 하나의 UE 인스턴스에 4개의 병렬 환경으로 수행하였으며 이후 AWS 클라우드 기반의 대규모 병렬 학습 환경을 구축했습니다. 이를 통해 수십 개의 언리얼 엔진 인스턴스로부터 데이터를 동시 수집하고 정책을 업데이트하는 고성능 학습 파이프라인을 구현했습니다.


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

DynamicEQS는 Schola 플러그인을 기반으로 하는 **재사용 가능한 UE5 전용 RL-EQS 통합 레이어**입니다. 게임 프로젝트가 Schola의 저수준 gRPC/ONNX 처리를 직접 다루지 않고 EQS 특화 추상 클래스만 상속하면 됩니다.

#### 플러그인 계층 구조

{{< img src="/images/project1/pluginarchi.png" 
        alt="" 
        class="max-w-full" 
        caption="Fig 2. 플러그인 계층 구조" >}}

#### 런타임 데이터 흐름


{{< img src="/images/project1/runtimedataflow.png" 
        alt="" 
        class="max-w-full" 
        caption="Fig 3. 런타임 데이터 플로우" >}}



#### EQS 가중치 주입 (ApplyWeightsToRequest)

정책 네트워크의 출력(7-dim Box action)이 실제 EQS 쿼리 파라미터로 주입되는 전체 흐름은 다음과 같습니다.

**에디터 설정**

1. `BP_Agent`(ADECharacter)에 `UDynamicEQSExecutor` 컴포넌트를 추가합니다.
2. `WeightParamNames` 배열에 EQS Query의 각 테스트에서 사용하는 파라미터 이름을 순서대로 입력합니다 (예: `EnemyObjectiveProximity`, `AllyObjectiveProximity`, …).
   이름이 비어 있으면 자동으로 `Weight0`, `Weight1` … 형식으로 폴백됩니다.
3. `UDEScholaAgent` 컴포넌트에서 `Actuator`로 `UDETacticalParameterActuator`를 지정합니다.

**런타임 데이터 흐름**

```
Python Policy (ONNX) → SimpleStepper::Step()
  → Observe : TacticalObserver → 170-dim 관측 벡터
  → Think   : ONNX Inference  → 7-dim Box action [-1, 1]
  → Act     : DETacticalParameterActuator::TakeAction()
               └─ ActionToEQSWeights()          // float[7] → FDEEQSWeightParameters
               └─ ADECharacter::UpdateTacticalWeights()
               └─ ADECharacter::PerformTacticalAction()
                    └─ DynamicEQSExecutor::ApplyWeightsToRequest()
                         └─ EnvQueryRequest.SetFloatParam(name, weight)
                    └─ AIController->MoveTo(BestLocation)
```

```cpp
// DynamicEQSExecutor.cpp — EQS 파라미터 주입
void UDynamicEQSExecutor::ApplyWeightsToRequest(FEnvQueryRequest& Request) const
{
    for (int32 i = 0; i < Weights.Num(); ++i)
    {
        const FName ParamName = (WeightParamNames.IsValidIndex(i) && !WeightParamNames[i].IsNone())
            ? WeightParamNames[i]
            : FName(*FString::Printf(TEXT("Weight%d"), i));
        Request.SetFloatParam(ParamName, Weights[i]);
    }
}
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


### **에이전트 입력 상태(State) 구성표**

| Index | Dim | 토큰 내용 | 정규화 및 상세 설명 |
| --- | --- | --- | --- |
| **[0 : 3]** | 3 | 자신 위치 | / (7500, 7500, 1000) |
| **[3 : 6]** | 3 | 자신 속도 | / (600, 600, 600) |
| **[6 : 7]** | 1 | 자신 체력 | raw [0, 1] |
| **[7 : 47]** | 40 | 아군 토큰 (8×5) | 상대 위치/8000(3) + 체력(1) + 생존 여부(1) |
| **[47 : 87]** | 40 | 적 토큰 (8×5) | 상대 위치/8000(3) + 시야 확보(1) + 신뢰도(1) |
| **[87 : 143]** | 56 | 거점 토큰 (8×7) | 상대 위치/15000(2) + 높이/1000(1) + 점유(1) + 점령 진행도(1) + 할당 여부(1) + 전략적 가치(1) |
| **[143 : 151]** | 8 | 아군 마스크 | 0=유효, 1=패딩 |
| **[151 : 159]** | 8 | 적 마스크 | 0=유효, 1=패딩 |
| **[159 : 167]** | 8 | 거점 마스크 | 0=유효, 1=패딩 |
| **[167 : 170]** | 3 | 전략 원-핫 | [assault, defend, support] |
| **TOTAL** | **170** |  |  |

---

> **마스크 처리:** Python 정책의 `_safe_mask()`는 모든 슬롯이 패딩일 때 슬롯 0을 강제 언마스크하여 MultiheadAttention의 NaN을 방지합니다. 마스크 임계값은 `> 0.5` (float 비교)로, `0.0=유효 / 1.0=패딩` 의미론을 보존합니다.

<br>


**보상 구조 개요**

---

> **돌격: 거점의 확보**

목표는 적 거점 접근과 점령 완료입니다. 적 거점까지의 거리 감소분에 비례한 접근 보상을 매 스텝 부여하고, 거점 반경 내 진입 시 추가 존재 보너스를 부여합니다. 점령이 완료되면 즉시 `PostCaptureMomentumDuration` 스텝 동안 모멘텀 보너스가 활성화되어, 점령 후 제자리에 머무는 대신 다음 거점으로 계속 전진하도록 유도합니다.

```
# Assault reward (per step)
reward = 0
if distance_to_target_base decreased:
    reward += approach_reward * delta_distance
if agent inside target_base radius:
    reward += zone_presence_bonus
if base just captured:
    activate momentum_bonus for PostCaptureMomentumDuration steps
    reward += capture_bonus
if momentum active:
    reward += momentum_bonus  # encourages moving to next base
```

---

> **방어: 거점의 유지**

목표는 아군 거점 유지와 거점 내 적 격퇴입니다. 아군 거점 반경 내에 위치할 때 기본 존재 보상이 부여됩니다. 거점 내에서 적에게 데미지를 받으면 추가 내구도 보너스(`ZoneDurabilityBonus`)가 지급되어, 거점에서 물러나지 않고 버티는 행동을 강화합니다. 아군 거점이 없는 상황에서는 중립/적 거점 접근으로 목표가 전환됩니다.

```
# Defend reward (per step)
reward = 0
if agent inside friendly_base radius:
    reward += zone_presence_bonus
    if agent took damage this step:
        reward += zone_durability_bonus   # reward staying under fire
if no friendly base exists:
    if distance_to_neutral_or_enemy_base decreased:
        reward += approach_reward * delta_distance  # fallback objective
```

---

> **지원: 아군의 유지**

목표는 체력이 낮은 아군을 추적하고 힐링하며 후방을 유지하는 것입니다. 매 스텝 부상 아군 탐색을 수행하되, 잦은 타겟 전환으로 인한 진동 행동을 막기 위해 5스텝 캐시를 적용합니다. 캐시된 아군이 현재 가장 낮은 체력이 아니더라도 5스텝이 지나기 전까지는 교체하지 않습니다. 아군 뒤편에 위치하면 후방 포지셔닝 보너스를 받으며, 아군이 부상 중인 상황에서 직접 킬을 시도하면 역할 이탈 패널티가 부과됩니다.

```
# Support reward (per step)
reward = 0
if staleness_counter >= 5 or no cached_target:
    cached_target = ally with lowest health
    staleness_counter = 0
else:
    staleness_counter++

if distance_to_cached_target decreased:
    reward += approach_reward * delta_distance
if heal applied to ally:
    reward += heal_reward * heal_amount
if agent positioned behind cached_target (rear arc):
    reward += rear_positioning_bonus
if cached_target.health < threshold and agent attempted kill:
    reward -= role_deviation_penalty
```



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

#### 학습 모니터링 (TensorBoard)

Ray RLlib이 각 이터레이션의 보상, 정책 손실, KL 발산, Entropy 등을 자동으로 TensorBoard에 기록합니다.

{{< img src="/images/project1/reward.png"
        alt=""
        class="max-w-4xl"
        caption="그림 7. Reward" >}}

{{< img src="/images/project1/klenvf.png"
        alt=""
        class="max-w-4xl"
        caption="그림 8. KL, Entropy, VF Explained" >}}

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

#### Gameplay Tag & BT 태스크 연동

`BTTask_DEAttackAbility` · `BTTask_DEHealAbility` 양쪽 모두 내부 구현이 아닌 **Gameplay Tag 조회**를 통해 능력을 활성화합니다. Behavior Tree는 능력 교체·파라미터 변경에 완전히 무관하며, 풀오토 공격은 `TickTask`에서 매 프레임 재활성화하는 방식으로 구현됩니다.



---

### 5. 듀얼 모드 아키텍처 (Dual-Mode Architecture)

모든 주요 컴포넌트는 단일 UE5 바이너리 내에서 **학습 모드(Training)** 와 **추론 모드(Inference)** 를 동시에 지원하도록 설계했습니다. 학습이 끝난 ONNX 모델을 별도의 빌드 없이 동일한 UE5 환경에서 즉시 실행하고 검증할 수 있습니다.

#### 핵심 설계: `UDEScholaAgent`

에이전트 컴포넌트 `UDEScholaAgent`가 두 모드를 하나의 인터페이스로 추상화합니다. `AgentMode` 프로퍼티 하나로 행동 파이프라인 전체가 분기됩니다.

#### 모드 비교

| 항목 | Training Mode | Inference Mode |
|---|---|---|
| **정책 실행 주체** | Python RLlib (gRPC) | UE5 내장 ONNX (NNE) |
| **스테퍼** | `GymConnector` (Schola 통신 루프) | `USimpleStepper` (TickComponent) |
| **EQS 실행** | Synchronous (블로킹) | Async (BB 경유) |
| **보상 계산** | `UDERewardSubsystem` 매 스텝 | 없음 |
| **에피소드 관리** | Schola `AutoResetType::SAME_STEP` | 레벨 재시작 |
| **BT 역할** | 전투(`Attack`/`Heal`) 태스크만 실행 | 전투 + 이동 모두 실행 |

#### 모드별 실행 분기: `BeginPlay` & `PerformTacticalAction()`

```cpp
// DynamicEQSAgentComponent.cpp — BeginPlay에서 모드 분기
void UDynamicEQSAgentComponent::BeginPlay()
{
    Super::BeginPlay();
    if (AgentMode == EDynamicEQSAgentMode::Inference)
    {
        // ONNX 정책 초기화 후 SimpleStepper 생성
        Stepper = NewObject<USimpleStepper>(this);
        Stepper->Init({this}, InferencePolicyObject);
    }
    // Training 모드: Schola GymConnector가 외부에서 Observe/Act 호출
}

// DynamicEQSAgentComponent.cpp — TickComponent (Inference only)
void UDynamicEQSAgentComponent::TickComponent(...)
{
    Super::TickComponent(...);
    if (AgentMode == EDynamicEQSAgentMode::Inference && Stepper)
        Stepper->Step();  // Observe → ONNX Infer → Act
}
```

```cpp
// ADECharacter::PerformTacticalAction() — EQS 실행 방식 분기
void ADECharacter::PerformTacticalAction()
{
    if (ScholaAgent->AgentMode == EDynamicEQSAgentMode::Training)
    {
        // Training: 동기 EQS (Schola 스텝 버짓 내에서 즉시 완료)
        FVector BestLoc;
        EQSExecutor->ExecuteQuerySynchronous(CurrentEQSWeights, BestLoc);
        AIController->MoveToLocation(BestLoc);
    }
    else
    {
        // Inference: Blackboard 경유 비동기 EQS → BTTask_DEMoveToEQSLocation
        EQSExecutor->ExecuteQuery(CurrentEQSWeights,
            [this](FVector BestLoc){ WriteWeightsToBB(BestLoc); });
    }
}
```

<br>

---

## 기술적 난제 및 해결 전략 (Problem Solving)

<br>

### Problem 1: UE5 환경과 Rllib 환경의 통신 표준 프레임워크의 필요성

기존 UE5에서 제공하는 강화학습 프레임워크인 **Learning Agent**는 UE5 로컬에서만 동작하기에 Python Ray Rllib의 분산 강화학습이라는 이점을 활용할 수 없었습니다.

<br>

#### Goal
UE5와 Python(Ray RLlib) 간의 고성능 강화학습 파이프라인을 구축하기 위해 안정적인 통신 수단과 표준 포멧이 포함된 프레임워크의 확보.

<br>

#### Solution

AMD의 오픈소스 라이브러리 [**Schola**](https://github.com/GPUOpen-LibrariesAndSDKs/Schola)를 프레임워크로 채택하였으며 아래와 같은 이점을 얻었습니다.

> #### Schola
> The Schola project is an effort to build a toolkit/plugin for controlling Objects in Unreal with Reinforcement Learning. It provides tools to help the user create Environments, define Agents, connect to python based RL Frameworks (e.g. Gym, RLLib or Stable Baselines 3), and power NPCs with RL during games.

* **표준 포멧 및 환경 구성**: Schola는 UE5에서 RL을 위한 관측(UBoxObserver)과 액션(UBoxActuator) 인터페이스를 제공하며 에이전트를 위한 전용 컨트롤러(AAbstractTrainer)와 Rllib와의 주요 통신 인터페이스(AStaticScholaEnvironment)를 통해 직관적인 RL 환경 구성을 가능하게 합니다.


* **래핑 및 gRPC 통신 브릿지:** Schola는 UE5 내의 데이터를 Python의 gym.Env 형태로 래핑하고 gRPC 프로토콜 기반의 직렬화를 통해 저지연으로 Rllib 환경과 통신하는 API를 제공합니다.

<br>

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

### Problem 2: 멀티 에이전트 강화학습 환경(Schola + RLlib)에서의 에이전트 개별 사망 처리 결함

{{< img src="/images/project1/problem2.png" 
        alt="" 
        class="max-w-full" 
        caption="그림 5. 프리징 현상의 원인과 해결" >}}

* **에피소드 멈춤(Episode Freeze)**: 특정 에이전트가 먼저 사망할 경우, RLlib은 해당 에이전트의 액션을 전송하지 않지만 Unreal Engine Schola는 모든 에이전트의 액션을 기다리며 대기 상태에 빠지는 통신 불일치 발생했습니다.

* **부활 루프(Death-resurrection loop)**: SAME_STEP 모드에서 사망한 에이전트가 유효한 상태 없이 즉시 리셋되어 다시 사망하는 무한 루프 현상 발생했습니다.

<br>

#### 근본 원인 분석: 두 종료 시스템의 충돌

Schola 측(`AutoResetType::SAME_STEP`)과 Python 측(RLlib) 사이에 에피소드 종료 신호가 서로 모순되는 상태였습니다.

- **Schola 측**: 에이전트가 사망하면 `SAME_STEP` 정책에 의해 즉시 자동 리셋을 트리거했습니다.
- **Python 측**: RLlib은 혼합 궤적(서로 다른 에피소드의 데이터가 한 배치에 섞이는 것)이 생기지 않도록, 모든 에이전트의 종료 신호(`done`)를 억제하고 있었습니다.

결과적으로 사망한 에이전트는 Schola가 부활시키자마자 다시 사망하는 무한 루프에 빠지고, 그 루프가 Schola의 스텝 예산(step budget) 전체를 소비해버렸습니다. 생존한 에이전트들은 스텝 버짓이 고갈된 Schola의 멀티에이전트 동기화 장벽(step barrier)에 막혀 영원히 다음 액션을 받지 못하는 상태가 되었습니다.

<br>

#### Goal
시차를 두고 발생하는 에이전트 사망(Staggered Death) 상황에서도 시스템 중단 없는 안정적인 학습 환경 구축
* 에이전트별 사망 시점이 달라도 전체 에피소드가 정상적으로 종료(__all__=True)되도록 보장.
* 사망한 에이전트의 관측값이나 보상이 학습 데이터에 오염(NaN 발생 등)을 일으키지 않도록 필터링 시스템 구현.

<br>


#### Solution: Python(통신 계층)과 C++(엔진 계층)의 이중 레이어 수정


#### Python (Schola Wrapper):

No-op Padding: 사망한 에이전트의 빈자리에 무효 액션(noop)을 삽입하여 Unreal이 항상 전체 에이전트의 액션을 수신하도록 보정.

Data Filtering: Unreal로부터 받은 응답 중, 이미 사망한 에이전트의 관측값/보상/정보를 필터링하여 RLlib에 전달.

<br>

**C++ (Unreal Plugin):**

Dead Agent Snapshot: Step() 실행 전 사망한 에이전트 상태를 기록하고, 실행 후 터미널 플래그(Terminal Flags)를 재복구하여 상태 덮어쓰기 및 부활 루프 방지.

Action Filter: 사망한 에이전트의 액션이 물리 엔진 및 로직에 영향을 주지 않도록 제외 처리.

<br>

#### Result
* 단위 테스트 통과: 총 10종의 Standalone 테스트(No-op 생성, 패딩 프로토콜 등) 100% 통과.

* 학습 안정성 확보: 실제 Unreal 통합 환경에서 에피소드 정지 현상 해결 및 정상적인 에피소드 리셋 주기 확인.

* 데이터 무결성: 보상 체계에서의 NaN 발생 및 Trajectory 누수 차단 확인.

* 관련 문제 Schola OpenSource에 PR 완료.

{{< img src="/images/project1/pr.png" 
        alt="" 
        class="max-w-3xl" 
        caption="그림 6. Pull Request" >}}

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
본 프로젝트는 셀프플레이(Self-Play) 방식으로 학습을 진행했습니다. 고정된 규칙 기반 AI를 상대하는 것이 아니라, 양 팀이 서로의 전략에 반응하며 함께 진화하는 구조입니다. 이는 기술적으로 훨씬 높은 난이도를 요구하는데, 상대방이 고정된 목표(fixed target)가 아니라 끊임없이 변화하는 이동 목표(moving target)이기 때문입니다.