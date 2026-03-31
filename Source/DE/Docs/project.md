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

<hr style="border: 0; height: 1px; background: #b3b3b3;">

## 개요 (Overview)

본 프로젝트는 UE5 NPC의 전술적 포지셔닝을 하드코딩 없이 강화학습으로 자동 최적화하는 Dynamic EQS 플러그인을 설계·구현하고, 5v5 팀 기반 거점 점령전 환경에서 MAPPO 학습을 통해 검증했습니다.

Schola(gRPC) 위에 RL-EQS 통합 미들 레이어를 플러그인으로 추상화하여 다른 UE5 프로젝트에서도 재사용 가능한 구조로 설계했으며, AWS EC2 + Ray Autoscaler 기반의 병렬 학습 파이프라인까지 1인으로 전 과정을 구축했습니다.

<div style="margin-top: 40px;"></div>

{{< gif-grid urls="/gifs/project1/project1_title.gif, /gifs/project1/project1_battle.gif" widths="50%, 50%" >}}


<hr style="border: 0; height: 1px; background: #b3b3b3;">

## 시스템 아키텍처 (System Architecture)


{{< img src="/images/project1/archi.png"
        alt=""
        class="max-w-full"
        caption="Fig 1. 시스템 아키텍처 및 계층적 포지셔닝 워크플로우" >}}




<hr style="border: 0; height: 1px; background: #b3b3b3;">

## 기술 스택 (Tech Stack)

| Category | Technologies |
|---|---|
| **Game Engine** | Unreal Engine 5.6 (C++17) |
| **RL Framework** | Ray RLlib 2.7, PyTorch |
| **UE5-Python Bridge** | Schola Plugin (gRPC-based) |
| **Neural Network Inference** | ONNX Runtime via UE5 NNE (Neural Network Engine) |
| **Ability System** | UE5 Gameplay Ability System (GAS) — GameplayAbility, GameplayEffect, GameplayTag |
| **Cloud & Infra** | AWS (EC2, EKS), Docker (Linux) |
| **Communication** | gRPC (Schola protocol) |
| **Monitoring** | TensorBoard |





<hr style="border: 0; height: 1px; background: #b3b3b3;">

## 훈련 환경 (Training Environment)

**5 vs 5 팀 기반 거점 점령전**으로, 맵 상에 5개의 거점(Capture Point)이 배치됩니다.
더 많은 거점을 점령·유지하여 목표 점수를 먼저 달성하는 팀이 승리합니다.

---


### 클래스 역할 설계

각 에이전트는 매 에피소드마다 **Strike / Vanguard / Support** 클래스를 부여받습니다.
클래스마다 요구되는 포지셔닝 전략이 근본적으로 다르므로, 각 클래스에
**독립된 정책 네트워크**를 할당하고, 역할에 특화된 보상 함수를 적용했습니다.


{{< gif-grid urls="/gifs/project1/project1_strike.gif, /gifs/project1/project1_vanguard.gif, /gifs/project1/project1_support.gif" >}}

| 클래스 | 역할 | 핵심 포지셔닝 목표 |
|---|---|---|
| **Strike** | 원거리 딜러 | 적 거점 접근 + 사거리 유지 |
| **Vanguard** | 근접 탱커 | 전열 유지 + 근접 교전 |
| **Support** | 후방 힐러 | 부상 아군 추적 + 후방 포지션 |


---


### ScriptedAI 상대 학습

Blue 팀(RL)은 하드코딩 EQS 가중치 + 상태 머신으로 구동되는 **Red 팀(ScriptedAI)** 을
상대로 학습합니다.

현재 환경은 3가지 클래스의 에이전트들이 복합적으로 섞여있어 Self-Play를 사용하기에 적합하지 않아, ScriptedAI를 선택했습니다. ScriptedAI를 통해 고정된 난이도 기준점을 제공하여 RL 에이전트가 안정적으로
기초 전략을 학습할 수 있었습니다.

---

### 커리큘럼 티어 시스템

ScriptedAI는 **3단계 난이도 티어**(1=Basic, 2=Standard, 3=Aggressive)로 구분됩니다.
RL 팀의 에피소드 승률이 일정 기간 동안 승격 임계값을 초과하면 자동으로 다음 티어로
전환됩니다.

| 티어 전환 | 승격 임계값 |
|---|---|
| Tier 1 → 2 | 55% 이상 |
| Tier 2 → 3 | 65% 이상 |

낮은 난이도 과적합(overfitting)을 방지하고 점진적으로 더 강한 상대에 노출시키기 위해
도입했습니다. 티어 정보는 `scripted_ai_config.json`에 기록되며, UE5의
`LoadTierFromConfig()`가 다음 에피소드 리셋 시 자동으로 적용합니다.

<div style="margin-top: 40px;"></div>

<hr style="border: 0; height: 1px; background: #b3b3b3;">


### 관측 공간


`FDEObservationV2::ToFlatArray()`가 생성하는 218-dim 엔티티 중심(Entity-Centric) 벡터입니다. 아군·적·거점을 고정 크기 슬롯 토큰으로 인코딩하고, 패딩 마스크를 별도로 제공해 Python MultiheadAttention이 유효 엔티티만 처리하도록 합니다.

<div style="margin-top: 40px;"></div>

#### **에이전트 입력 상태(State) 구성표**

| Index | Dim | 토큰 내용 | 정규화 및 상세 설명 |
| --- | --- | --- | --- |
| **[0 : 7]** | 7 | 자신 토큰 | 위치/7500(3) + 속도/600(3) + 체력(1) |
| **[7 : 71]** | 64 | 아군 토큰 (8×8) | 상대 위치/8000(3) + 체력(1) + 생존 여부(1) + 클래스 원-핫(3) |
| **[71 : 135]** | 64 | 적 토큰 (8×8) | 상대 위치/8000(3) + 체력(1) + 가시성(1) + 클래스 원-핫(3) |
| **[135 : 191]** | 56 | 거점 토큰 (8×7) | 상대 위치/15000(2) + 높이/1000(1) + 점유(1) + 점령 진행도(1) + 할당 여부(1) + 전략적 가치(1) |
| **[191 : 199]** | 8 | 아군 마스크 | 0=유효, 1=패딩 |
| **[199 : 207]** | 8 | 적 마스크 | 0=유효, 1=패딩 |
| **[207 : 215]** | 8 | 거점 마스크 | 0=유효, 1=패딩 |
| **[215 : 218]** | 3 | 클래스 원-핫 | [strike, vanguard, support] |
| **TOTAL** | **218** |  |  |


> **마스크 처리:** Python 정책의 `_safe_mask()`는 모든 슬롯이 패딩일 때 슬롯 0을 강제 언마스크하여 MultiheadAttention의 NaN을 방지합니다. 마스크 임계값은 `> 0.5` (float 비교)로, `0.0=유효 / 1.0=패딩` 의미론을 보존합니다.

---


### 보상 구조

<div style="margin-top: 40px;"></div>

<font size="4">**S T R I K E**</font>

목표는 원거리에서 높은 데미지를 유지하며 거점을 점령하는 것입니다. 적 거점까지의 거리 감소분에 비례한 접근 보상을 매 스텝 부여하고, 거점 반경 내 진입 시 추가 존재 보너스를 부여합니다. 적과 너무 가까운 경우 원거리 역할 이탈 페널티가 부과됩니다.

{{< code lang="cpp" label="Strike reward (per step)" width="100%" height="250px" align="right" >}}
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
if distance_to_enemy < MinCombatRange:
    reward -= too_close_enemy_penalty  # ranged: maintain combat distance
{{< /code >}}

---

<font size="4">**V A N G U A R D**</font>

목표는 전열에서 근접 전투를 수행하며 거점을 점령하는 것입니다. 적 거점 접근 및 점령 방식은 Strike와 동일하게 적용됩니다. 거점 내에서 근접 사거리에 적이 있을 때 추가 근접 보너스가 지급되어, 전선을 유지하며 적과 밀착하는 행동을 강화합니다.

{{< code lang="cpp" label="Vanguard reward (per step)" width="100%" height="250px" align="right" >}}
reward = 0
if distance_to_target_base decreased:
    reward += approach_reward * delta_distance
if agent inside target_base radius:
    reward += zone_presence_bonus
    if enemy within melee range:
        reward += melee_range_bonus  # frontline tank: reward close engagement
if base just captured:
    activate momentum_bonus for PostCaptureMomentumDuration steps
    reward += capture_bonus
if momentum active:
    reward += momentum_bonus
{{< /code >}}

---

<font size="4">**S U P P O R T**</font>


목표는 데미지가 심한 아군을 추적하고 치유하며 후방 포지션을 유지하는 것입니다. 매 스텝 부상 아군 탐색을 수행하되, 잦은 타겟 전환으로 인한 진동 행동을 막기 위해 5스텝 캐시를 적용합니다. Support를 제외한 아군 뒤편에 위치하면 후방 포지셔닝 보너스를 받습니다.


{{< code lang="cpp" label="Support reward (per step)" width="100%" height="250px" align="right" >}}
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
{{< /code >}}

---

### 팀 보상 믹싱 (Team Reward Mixing — MAPPO Cooperative Signal)

개별 에이전트 보상은 팀 평균 보상과 혼합되어 협동 행동을 강화합니다. `DERewardSubsystem`에서 매 스텝 계산되며, 혼합 비율은 `TeamRewardMixingRatio`로 제어됩니다.

```
final_reward = (1 - α) × individual_reward + α × team_average_reward
```

| 파라미터 | 값 | 의미 |
| :--- | :--- | :--- |
| `TeamRewardMixingRatio` (α) | **0.2** | 80% 개별 + 20% 팀 평균 |

팀 평균은 자신을 제외한 같은 팀 에이전트들의 `LastIndividualStepReward` 평균입니다. 팀 에이전트가 1명일 때는 믹싱이 적용되지 않습니다. 이 구조는 MAPPO(Multi-Agent PPO)의 cooperative reward shaping으로, 개별 역할 최적화와 팀 단위 승리 목표 사이의 균형을 유지합니다.

> **구현 위치:** `DERewardSubsystem.cpp` — `CalculateStepReward()` 함수 하단, 개별 보상 계산 후 팀 믹싱 적용

---

### UE5 → Python 보상 파이프라인 (Reward Pipeline)

보상은 **C++ (UE5)** 에서 계산되고 **Python (RLlib)** 에서 정규화된 후 PPO 업데이트에 사용됩니다.

```
[UE5 C++]                              [Python RLlib]
DERewardSubsystem                      process_reward()
  ├─ 역할별 보상 계산                      ├─ reward_scale (×0.01)
  │  (Strike/Vanguard/Support)            └─ reward_clip (±5.0)
  ├─ 팀 보상 믹싱 (α=0.2)                       │
  └─ RewardScale/Clamp                         ▼
       │                                 PPO Policy Update
       ▼                                   ├─ Actor: EntityCentricPolicy
  gRPC → Python env_wrapper                │    (Self-Attention on entity tokens)
                                           └─ Critic: Dual Value Estimation
                                                V = α·V_local + (1-α)·V_central
```

**단계별 흐름:**

1. **C++ 보상 계산:** `DERewardSubsystem`이 매 스텝 역할별 보상(접근, 거점, 전투 등)을 계산하고, 팀 보상 믹싱(80:20)을 적용한 뒤 `RewardScale`/`Clamp`으로 1차 정규화합니다.
2. **gRPC 전송:** Schola 플러그인이 보상을 gRPC를 통해 Python 환경(`DEEntityCentricEnv`)으로 전달합니다.
3. **Python 정규화:** `process_reward()`가 `reward_scale=0.01`로 스케일링하고 `±5.0`으로 클리핑하여 PPO 학습 안정성을 확보합니다.
4. **Dual Critic 평가:** `EntityCentricRLlibModel`의 value function이 학습 가능한 혼합 계수(α, sigmoid 초기값 0.5)를 사용하여 V_local(226-dim 에이전트 관찰 기반 attention critic)과 V_central(71-dim 글로벌 팀 상태 기반 MLP critic)을 결합합니다. 세 역할 정책이 centralized critic을 공유합니다.



<hr style="border: 0; height: 1px; background: #b3b3b3;">

## 주요 기능 (Key Features)


### 1. Dynamic-EQS Plugin

**DynamicEQS**는 Schola 플러그인을 기반으로 하는 **재사용 가능한 UE5 전용 RL-EQS 통합 레이어**입니다. DynamicEQS를 사용하는 게임 프로젝트는 Schola의 저수준 gRPC/ONNX 처리를 직접 다루지 않고 EQS 특화 추상 클래스만 상속하면 됩니다.

DynamicEQS의 주요 클래스는 4가지로, **환경**, **에이전트**, **액션**, **관찰**을 담당합니다. Schola와 게임 프로젝트의 미들 계층에서 EQS 가중치를 설정하고 정책 네트워크와 매핑해주는 역할을 합니다.


{{< img src="/images/project1/pluginarchi.png"
        alt=""
        class="max-w-full"
        caption="Fig 2. 플러그인 계층 구조" >}}

{{< img src="/images/project1/flow3.png"
        alt=""
        class="max-w-full"
        caption="Fig 3. 학습/추론 런타임 데이터 플로우" >}}

---

<div style="margin-top: 40px;"></div>

<font size="4">**EQS 가중치 주입**</font>

정책 네트워크의 출력(7-dim Box action)이 실제 EQS 쿼리 파라미터로 주입되는 전체 흐름은 다음과 같습니다.

**에디터 설정**

1. `BP_Agent`(ADECharacter)에 `UDynamicEQSExecutor` 컴포넌트를 추가합니다.
2. `WeightParamNames` 배열에 EQS Query의 각 테스트에서 사용하는 파라미터 이름을 순서대로 입력합니다 (예: `EnemyObjectiveProximity`, `AllyObjectiveProximity`, …).
3. `UDEScholaAgent` 컴포넌트에서 `Actuator`로 `UDETacticalParameterActuator`를 지정합니다.


{{< img-grid-scaler
    src1="/images/project1/eqs1.png"
    cap1="Fig 3. DynamicEQSExecutor 컴포넌트"
    class1="w-full"

    src2="/images/project1/eqs2.png"
    cap2="Fig 4. EQS 에셋 구성"
    class2="w-3/4"
>}}


{{< code lang="cpp" label="EQS Parameter Injection" width="100%" height="250px" align="right" >}}
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
{{< /code >}}



---

<div style="margin-top: 40px;"></div>

<font size="4">**FInstancedStruct를 활용한 게임 로직 디커플링**</font>

플러그인이 게임 전용 타입(`AssignedBaseIndex` 등)을 직접 멤버로 갖지 않도록, `FInstancedStruct`로 외부 파라미터를 불투명하게 보관합니다.

플러그인이 게임 모듈에 대한 **컴파일 타임 의존성**을 가지면 다른 프로젝트에서 재사용이 불가능해지므로, 런타임 타입 정보를 보존하면서 모듈 간 의존성을 끊는 방법이 필요했습니다. `void*`는 타입 안전성이 없고, 인터페이스 패턴(`IExternalContext`)은 게임 측에 구현을 강제하여 플러그인 자체의 독립성을 훼손합니다. UE5의 `FInstancedStruct`는 USTRUCT 메타데이터를 보존하면서 불투명 저장이 가능해 이 요구사항에 부합했습니다.

{{< code lang="cpp" label="DynamicEQSAgentComponent.h" width="100%" height="250px" align="right" >}}
UPROPERTY(BlueprintReadWrite)
FInstancedStruct ExternalParams;  // 어떤 USTRUCT도 저장 가능

// DETacticalParameterActuator.cpp — 사용 시점에 게임 타입으로 캐스팅
const FDEAgentExternalContext* Ctx =
    AgentComponent->ExternalParams.GetPtr<FDEAgentExternalContext>();
if (Ctx)
    Weights.AssignedBaseProximity = ComputeBaseProximityWeight(Ctx->AssignedBaseIndex);
{{< /code >}}

이 패턴 덕분에 플러그인은 게임 헤더를 전혀 포함하지 않으며, 다른 프로젝트에서 그대로 재사용할 수 있습니다.

<hr style="border: 0; height: 1px; background: #b3b3b3;">


### 2. MAPPO (Multi-Agent PPO)


<div style="margin-top: 40px;"></div>


본 프로젝트는 **MAPPO(Multi-Agent PPO)** 를 채택하여 역할별 독립 정책(Actor) 3개(`strike_policy`, `vanguard_policy`, `support_policy`)와 공유 중앙집중식 Critic을 함께 학습합니다.

**중앙집중식 Critic (Centralized Critic)** — `CentralizedCritic`은 71-dim 전역 팀 상태(`FDETeamWorldState`: 아군 5명 위치·체력·전략 + 적 5명 위치·신뢰도 + 맵 상태)를 입력받아 팀 전체의 가치를 추정합니다. 개별 에이전트 관측만으로는 알 수 없는 팀-레벨 정보(아군 분포, 전체 거점 점령 상황 등)를 Critic이 직접 참조하므로 Advantage 추정의 분산이 감소합니다.

<div style="margin-top: 40px;"></div>

**Dual Value Estimation** 

각 `EntityCentricRLlibModel`은 로컬 Critic(`V_local`, 226-dim 에이전트 관측)과 중앙 Critic(`V_central`, 71-dim 전역 상태)을 학습 가능한 혼합 계수 α로 결합합니다.


$$V = \alpha \cdot V_{\text{local}}(\text{agent\_obs}[0:226]) + (1 - \alpha) \cdot V_{\text{central}}(\text{global\_state}[226:297])$$


α는 `sigmoid(_value_mix_logit)`로 초기화되며(초기값 0.5), 학습을 통해 각 역할에 최적인 로컬/전역 비중을 자동으로 결정합니다.

<div style="margin-top: 50px;"></div>

**Self-Attention의 역할**

Actor의 인코더에서 아군·적·거점 각 엔티티 그룹에 Intra-Set Self-Attention을 적용합니다(Zambaldi et al., 2018). 엔티티 토큰들이 서로를 참조하여 **"슬롯 3과 슬롯 5가 같은 거점 근처에 집결"** 같은 집합 내 공간 관계를 학습합니다. 이 문맥화된 표현이 이후 Cross-Attention의 입력으로 사용됩니다.

<div style="margin-top: 50px;"></div>

**Cross-Attention의 역할**

Self Token(자신 관측 7-dim의 임베딩)이 Query가 되고, Self-Attention을 거친 아군·적·거점 토큰이 Key/Value가 됩니다. Cross-Attention은 **"현재 나의 상태에서 각 엔티티가 얼마나 중요한가"** 를 가중합으로 집약하여 행동(EQS 가중치 7-dim)을 결정합니다.



{{< code lang="python" label="train.py" width="100%" height="250px" align="right" >}}
# 역할별 독립 정책 라우팅
STRATEGY_POLICY_NAMES = {0: "strike_policy", 1: "vanguard_policy", 2: "support_policy"}

config = config.multi_agent(
    policies={
        name: PolicySpec(config={"model": model_cfg})
        for name in STRATEGY_POLICY_NAMES.values()
    },
    policy_mapping_fn=_policy_mapping_fn,  # agent_id → class → policy
    count_steps_by="agent_steps",
)
{{< /code >}}


<hr style="border: 0; height: 1px; background: #b3b3b3;">




<font size="4">**평가 모드 (Evaluation Mode)**</font>

학습 중 저장된 체크포인트의 성능을 ScriptedAI 대전을 통해 정량적으로 검증하는 **Live Evaluation** 파이프라인(`eval_live.py`)입니다.

<div style="margin-top: 40px;"></div>

<font size="4">**구조 개요**</font>


학습 중 저장된 체크포인트 성능을 ScriptedAI 대전으로 정량 검증하는
`eval_live.py` 파이프라인입니다. 

UE5에 2개의 서브 환경을 동시 연결하여 두 개의 체크포인트(`best`, `latest`)를 병렬로 평가합니다. 에피소드 결과를 `win / loss / draw / timeout`으로 분류하고, 50회 에피소드 기준 승률을 `eval_results_<timestamp>.json`으로 저장합니다.

Ray 런타임 없이 `policy_state.pkl`에서 Actor 가중치만 추출하여
CPU 추론으로 즉시 실행 가능하도록 설계했습니다.


<hr style="border: 0; height: 1px; background: #b3b3b3;">



### 3. AWS 병렬 학습 환경 (AWS Parallel Training Environment)

로컬 단일 UE5 인스턴스의 한계를 넘어 클라우드 병렬 환경으로 확장하기 위해
AWS 기반 분산 학습 파이프라인을 구축했습니다. UE5와 학습 스크립트를 Linux
컨테이너로 패키징하고, Ray Autoscaler로 EC2 클러스터를 동적으로 관리합니다.

---

<font size="4">**전체 인프라 구성**</font>

{{< img src="/images/project1/aws_architecture.png"
        alt=""
        class="max-w-full"
        caption="Fig 10. AWS 병렬 학습 인프라 전체 구성도" >}}


---

<font size="4">**인프라 구성**</font>

| 컴포넌트 | 역할 |
|---|---|
| **Amazon ECR** | 학습 Docker 이미지 레지스트리 — 모든 노드가 동일 이미지를 Pull하여 환경 일관성 보장 |
| **EC2 Ray 클러스터** | Head(GPU, g4dn.xlarge) + Worker(CPU Spot, c5.2xlarge×4) 혼합 구성 |
| **Amazon S3** | 체크포인트 영구 저장 — s3fs FUSE 마운트로 노드 간 공유 |
| **W&B** | 이터레이션별 보상·승률·손실 지표 실시간 모니터링 |

Terraform으로 VPC·IAM·보안 그룹을 프로비저닝하고, Ray Autoscaler가
학습 부하에 따라 Worker 수를 자동 조절합니다.

---

<font size="4">**클러스터 설계 의도**</font>

Head 노드(GPU)는 정책 gradient 업데이트를, Worker 노드(CPU Spot)는
UE5 헤드리스 인스턴스 구동 및 롤아웃 수집을 전담합니다.
Worker를 Spot 인스턴스로 구성한 이유는 롤아웃 수집은 중단·재시작이
가능한 stateless 작업이기 때문입니다. Spot 중단 시 Ray Autoscaler가
자동으로 새 Worker를 기동하여 학습이 중단되지 않으며,
`idle_timeout_minutes: 10` 설정으로 유휴 Worker를 자동 종료해
비용을 절감합니다.

UE5는 기본적으로 디스플레이가 필요하므로, 클라우드 헤드리스 환경에서
구동하기 위해 Dockerfile에 가상 프레임버퍼(Xvfb)를 설정했습니다.
보안 강화를 위해 비루트 사용자(UID 1000)로 실행하고,
S3 자격증명은 정적 키 대신 IAM Role 기반으로 처리했습니다.

---

<font size="4">**학습 사이클 요약**</font>

1. Docker 이미지 빌드 → ECR Push
2. Ray 클러스터 기동 (`ray up`) → 학습 스크립트 제출
3. Worker 4개가 병렬로 UE5 롤아웃 수집 → `train_batch_size=4096` 배치 구성
4. Head GPU에서 PPO gradient 업데이트 → 10 이터레이션마다 S3 체크포인트 동기화
5. 신규 최고 승률 달성 시 `best/` 경로에 즉시 추가 동기화

`ray down` 시 모든 인스턴스가 즉시 종료되며, S3 체크포인트는 유지되어
재기동 시 이어서 학습할 수 있습니다.


<hr style="border: 0; height: 1px; background: #b3b3b3;">

## 기술적 난제 및 해결 전략 (Problem Solving)


### Problem 1: 멀티 에이전트 강화학습 환경(Schola + RLlib)에서의 에이전트 개별 사망 처리 결함

{{< img src="/images/project1/problem1.png"
        alt=""
        class="max-w-full"
        caption="Fig 14. 프리징 현상의 원인과 해결" >}}

**에피소드 멈춤(Episode Freeze)**: 특정 에이전트가 먼저 사망할 경우, RLlib은 해당 에이전트의 액션을 전송하지 않지만 Unreal Engine Schola는 모든 에이전트의 액션을 기다리며 대기 상태에 빠지는 통신 불일치 발생했습니다.

**부활 루프(Death-resurrection loop)**: SAME_STEP 모드에서 사망한 에이전트가 유효한 상태 없이 즉시 리셋되어 다시 사망하는 무한 루프 현상 발생했습니다.



---

<font size="4">**Cause**</font>

**두 종료 시스템의 충돌**

Schola 측(`AutoResetType::SAME_STEP`)과 Python 측(RLlib) 사이에 에피소드 종료 신호가 서로 모순되는 상태였습니다.

- **Schola 측**: 에이전트가 사망하면 `SAME_STEP` 정책에 의해 즉시 자동 리셋을 트리거했습니다.
- **Python 측**: RLlib은 혼합 궤적(서로 다른 에피소드의 데이터가 한 배치에 섞이는 것)이 생기지 않도록, 모든 에이전트의 종료 신호(`done`)를 억제하고 있었습니다.

결과적으로 사망한 에이전트는 Schola가 부활시키자마자 다시 사망하는 무한 루프에 빠지고, 그 루프가 Schola의 스텝 예산(step budget) 전체를 소비해버렸습니다. 생존한 에이전트들은 스텝 버짓이 고갈된 Schola의 멀티에이전트 동기화 장벽(step barrier)에 막혀 영원히 다음 액션을 받지 못하는 상태가 되었습니다.

---

<font size="4">**Goal**</font>

시차를 두고 발생하는 에이전트 사망(Staggered Death) 상황에서도 시스템 중단 없는 안정적인 학습 환경 구축
* 에이전트별 사망 시점이 달라도 전체 에피소드가 정상적으로 종료(__all__=True)되도록 보장.
* 사망한 에이전트의 관측값이나 보상이 학습 데이터에 오염(NaN 발생 등)을 일으키지 않도록 필터링 시스템 구현.

---

<font size="4">**Solution**</font>

**Python(통신 계층)과 C++(엔진 계층)의 이중 레이어 수정**

Python (Schola Wrapper):
* No-op Padding: 사망한 에이전트의 빈자리에 무효 액션(noop)을 삽입하여 Unreal이 항상 전체 에이전트의 액션을 수신하도록 보정.
* Data Filtering: Unreal로부터 받은 응답 중, 이미 사망한 에이전트의 관측값/보상/정보를 필터링하여 RLlib에 전달.


C++ (Unreal Plugin):
* Dead Agent Snapshot: Step() 실행 전 사망한 에이전트 상태를 기록하고, 실행 후 터미널 플래그(Terminal Flags)를 재복구하여 상태 덮어쓰기 및 부활 루프 방지.
* Action Filter: 사망한 에이전트의 액션이 물리 엔진 및 로직에 영향을 주지 않도록 제외 처리.

---

<font size="4">**Result**</font>

* 단위 테스트 통과: 총 10종의 Standalone 테스트(No-op 생성, 패딩 프로토콜 등) 100% 통과.

* 학습 안정성 확보: 실제 Unreal 통합 환경에서 에피소드 정지 현상 해결 및 정상적인 에피소드 리셋 주기 확인.

* 데이터 무결성: 보상 체계에서의 NaN 발생 및 Trajectory 누수 차단 확인.

* 관련 문제 Schola 오픈소스에 PR 제출.

{{< img src="/images/project1/pr.png"
        alt=""
        class="max-w-3xl"
        caption="Fig 6. Pull Request" >}}


<hr style="border: 0; height: 1px; background: #b3b3b3;">



### Problem 2: 엔티티 간 관계 정보 손실

{{< img src="/images/project1/problem2.png"
        alt=""
        class="max-w-full"
        caption="Fig 15. 엔티티 관계 정보 손실과 어텐션 도입을 통한 해결" >}}


학습된 정책이 "적 2명이 같은 거점에 집결" 같은 엔티티 간 공간 패턴을 인식하지 못하는 문제가 관찰되었습니다. 에이전트가 아군이 이미 점령 중인 거점에 중복 배치되는 비효율이 반복되었고, 보상 구조만으로는 이 현상을 충분히 정의하기 어려웠습니다.

---

<font size="4">**Cause**</font>


218-dim 관측 벡터에서 각 엔티티 슬롯은 선형 인코더(`nn.Linear`)를 통해 독립적으로 임베딩됩니다. 이후 Self Token이 Cross-Attention으로 엔티티 집합을 조회하지만, Query가 Self Token 1개이므로 **엔티티 간 상대적 관계**(밀집도, 협공 패턴, 동일 거점 중복)는 Attention weight에 반영되지 않습니다. Cross-Attention의 출력은 "각 엔티티가 Self에게 얼마나 중요한가"의 가중합이지, "엔티티들이 서로 어떤 관계인가"의 정보는 아닙니다.

---

<font size="4">**Solution**</font>

**Intra-Set Self-Attention (Zambaldi et al., 2018)**

각 엔티티 그룹(아군 / 적 / 거점)에 대해, Cross-Attention 이전에 **Self-Attention 레이어**를 삽입하여 엔티티들이 서로를 참조하도록 했습니다. Self-Attention을 거친 엔티티 토큰은 "나와 같은 거점 근처에 있는 아군이 2명이다"와 같은 문맥 정보를 내포하게 되며, 이후 Cross-Attention에서 Self Token이 이 **문맥화된** 엔티티 정보를 집약합니다.



{{< code lang="python" label="policy.py — Relational Self-Attention pipeline" width="100%" height="250px" align="right" >}}
# 1. 선형 인코딩: 원본 특성 → hidden 차원
a_enc = self.ally_enc(allies)                  # (B, 8, 64)

# 2. Self-Attention: 아군 토큰끼리 상호 참조
#    → "슬롯 3과 슬롯 5가 같은 거점 근처에 있다" 등의 관계를 학습
a_rel, _ = self.ally_self_attn(a_enc, a_enc, a_enc,
                               key_padding_mask=ally_mask)

# 3. Residual + LayerNorm: 원본 정보 보존 + 학습 안정화
a_enc = self.ally_ln(a_enc + a_rel)            # (B, 8, 64)

# 4. Cross-Attention: Self Token이 문맥화된 아군 정보를 집약
a_ctx, _ = self.ally_attn(q, a_enc, a_enc,
                          key_padding_mask=ally_mask)  # (B, 1, 64)
{{< /code >}}



**패딩 마스크 처리**: C++ 관측 레이아웃의 `0=유효, 1=패딩` 마스크를 Self-Attention과 Cross-Attention 양쪽에 동일하게 적용합니다. `_safe_mask()`가 모든 슬롯이 패딩인 경우 슬롯 0을 강제 언마스크하여 NaN을 방지합니다. C++ 측 수정 없이 Python 정책만으로 완결됩니다.



{{< code lang="python" label="policy.py — safe mask" width="100%" height="150px" align="right" >}}
def _safe_mask(m: torch.Tensor) -> torch.Tensor:
    all_masked = m.all(dim=1, keepdim=True)   # (B, 1)
    return m & ~all_masked                     # 모든 슬롯 패딩 시 슬롯 0 언마스크
{{< /code >}}


**설계 제약과 Trade-off**

| 항목 | 값 |
|---|---|
| 파라미터 증가 | 168K → 268K (+60%) |
| 추론 레이턴시 | < 2ms (0.3초 스텝 예산 대비 0.7%) |
| ONNX 호환성 | opset 14 — UE5 NNE 변경 없음 |
| C++ 수정 | 없음 (패딩 마스크 레이아웃 재사용) |

**정량적 비교:** Self-Attention 도입 전후 ablation 비교 결과는 별도 실험 후 추가 예정입니다.




<hr style="border: 0; height: 1px; background: #b3b3b3;">



### Problem 3: 스텝 속도와 에이전트 이동 간의 타이밍 불일치

{{< img src="/images/project1/problem3.png"
        alt=""
        class="max-w-full"
        caption="Fig 16. 스로틀링 제어" >}}

학습 환경에서 `AGymConnectorManager`의 `Tick()`이 매 프레임(60Hz+) `Connector->Step()`을 호출하여, EQS 이동이 완료되기 전에 다음 스텝이 실행되는 문제가 발생했습니다. 에이전트가 목적지에 도달하기 전에 새로운 EQS 목표 위치가 덮어써지면서 이동이 취소되어 목표 지점에 도달하지 못한 채 관측이 수집되면서 학습 데이터의 품질이 저하되었습니다.

---

<font size="4">**Goal**</font>

스텝 주기를 에이전트의 EQS 기반 이동 완료 시간에 맞게 조율하여, 에이전트가 목적지까지 실제로 이동한 후 관측이 수집되도록 보장합니다.

---

<font size="4">**Solution**</font>

**`AGymConnectorManager`를 오버라이드 대상으로 선택한 근거**

Schola의 학습 루프 진입점 구조는 다음과 같습니다.

```
AGymConnectorManager::Tick()
    └─ UAbstractGymConnector::Step()          ← 1회 학습 스텝
           ├─ ResolveEnvironmentStateUpdate()  ← Python에서 액션 수신 (gRPC, ~10ms 블로킹)
           ├─ HandleStep() / HandleReset()     ← 환경에 액션 적용
           └─ SubmitState()                    ← 관측·보상 Python으로 전송
```

`UAbstractGymConnector::Step()`은 Python과의 전체 한 사이클을 원자적으로 처리하며, 내부에서 `ResolveEnvironmentStateUpdate()`가 gRPC 응답을 블로킹 대기합니다. 따라서 **스텝 호출 빈도를 제어하는 유일한 지점**은 `Step()`을 직접 호출하는 `AGymConnectorManager::Tick()`입니다. 커넥터 내부 구현을 수정하지 않고 오직 `Tick()` 오버라이드만으로 스텝 속도를 외부에서 제어할 수 있다고 판단했습니다.

---

**`ADEGymConnectorManager` 구현**

`AGymConnectorManager`를 상속하는 커스텀 클래스 `ADEGymConnectorManager`를 구현하고, `AGymConnectorManager::Tick()` 대신 `AActor::Tick()`를 직접 호출하여 `Connector->Step()` 호출 빈도를 `StepInterval` 변수로 제어합니다.


{{< code lang="cpp" label="ADEGymConnectorManager.h" width="100%" height="150px" align="right" >}}
// 에디터에서 스텝 간격 조정 가능
UPROPERTY(EditAnywhere, Category = "Schola|Throttling",
    meta = (ClampMin = "0.01", ClampMax = "10.0"))
float StepInterval = 0.3f;  // 초 단위, 기본 2Hz
{{< /code >}}



`StepInterval = 0.3s`는 EQS 48샘플 쿼리 소요 시간(~5ms) + NavMesh 경로 계산(~2ms) + 에이전트 이동 거리(최대 탐색 반경 600cm, 이동 속도 600cm/s 기준 ~1초)를 고려한 값입니다. 에이전트가 목적지까지 충분히 이동한 후 관측이 수집되는 최소 주기를 프로파일링하여 결정했습니다.


{{< code lang="cpp" label="ADEGymConnectorManager.cpp — Implementation of Tick throttling" width="100%" height="250px" align="right" >}}
void ADEGymConnectorManager::Tick(float DeltaTime)
{
    // AGymConnectorManager::Tick 우회 — 직접 매 프레임 Step 호출 방지
    AActor::Tick(DeltaTime);

    if (!Connector) return;

    // 연결 대기 단계: Step은 내부적으로 CheckForStart()만 실행, 블로킹 없음
    if (Connector->IsNotStarted())
    {
        Connector->Step();
        return;
    }

    // 실행 단계: StepInterval마다 1회 Step 호출
    if (Connector->IsRunning())
    {
        // 에디터 백그라운드 전환 후 DeltaTime 급등으로 인한 버스트 스텝 방지
        const float ClampedDelta = FMath::Min(DeltaTime, StepInterval);
        StepAccumulator += ClampedDelta;
        if (StepAccumulator >= StepInterval)
        {
            StepAccumulator = 0.0f;
            Connector->Step();
        }
    }
}
{{< /code >}}



에디터 내 `BP_GymConnectorManager`의 부모 클래스를 `ADEGymConnectorManager`로 변경하는 것만으로 적용이 완료됩니다. `StepInterval`을 에디터 디테일 패널에서 값을 직접 조정할 수 있어 재빌드 없이 타이밍을 튜닝할 수 있습니다.

---

<font size="4">**Result**</font>

`StepInterval = 0.3s` 기준으로 에이전트가 EQS 목적지에 완전히 도달한 뒤 다음 관측이 수집되어 학습 데이터 품질이 개선되었습니다. 에디터 백그라운드 전환 시 발생하던 `DeltaTime` 급등에 의한 버스트 스텝 역시 `FMath::Min(DeltaTime, StepInterval)` 클램핑으로 차단되었습니다. `StepInterval` 단일 변수로 학습 속도와 이동 완료율 사이의 트레이드오프를 재빌드 없이 조절할 수 있는 구조가 완성되었습니다.


<hr style="border: 0; height: 1px; background: #b3b3b3;">



## 결과 (Results)

<font size="4">**학습 결과**</font>


총 2.5M(250만) 타임스텝에 걸쳐 MAPPO 기반 ScriptedAI 대전 학습을 수행했습니다.

{{< img src="/images/project1/result_reward.png"
        alt=""
        class="max-w-3xl"
        caption="Fig 7. reward" >}}

{{< img src="/images/project1/result_vf.png"
        alt=""
        class="max-w-3xl"
        caption="Fig 8. vf explained" >}}

{{< img src="/images/project1/result_entropy.png"
        alt=""
        class="max-w-3xl"
        caption="Fig 9. entropy" >}}


<div style="margin-top: 40px;"></div>

<font size="4">**핵심 수치 요약**</font>

| 지표 | 값 | 의미 |
|---|---|---|
| **episode_mean** | 0 → 30, 14 수렴 |
| **vf/explained_var** | 0.8 이상 | Critic이 미래 보상의 80% 이상을 설명 — 높은 상태 가치 예측력 |
| **entropy** | 초기 상승 후 하락 중 | 탐험→활용 전환 확인 |

**학습 종료 판단:** lr 스케줄러가 0에 도달하고 reward가 plateau에 진입한 2.5M 스텝에서 학습을 종료했습니다.

<div style="margin-top: 40px;"></div>
