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

본 프로젝트는 Unreal Engine 5 환경에서 강화학습 모델을 활용한 EQS(Environment Query system) 가중치
 업데이트를 지원하는 플러그인의 개발을 목표로 합니다.

본 프로젝트에서는 팀 기반 거점 점령전에서의 전략적 포지셔닝 최적화 환경을 대상으로 해당 플러그인을 활용하였습니다.


<hr style="border: 0; height: 1px; background: #b3b3b3;">

## 시스템 아키텍처 (System Architecture)


{{< img src="/images/project1/archi.png"
        alt=""
        class="max-w-full"
        caption="Fig 1. 시스템 아키텍처 및 계층적 포지셔닝 워크플로우" >}}


<hr style="border: 0; height: 1px; background: #b3b3b3;">

## 학습 환경 (Training Environment)

**5 vs 5 팀 기반 거점 점령전**으로, 맵 상에 5개의 거점(Capture Point)이 배치됩니다.

- **승리 조건:** 더 많은 거점을 점령하고 유지하여 목표 점수를 먼저 달성하는 팀이 승리합니다.
- **부활 규칙:** 개별 사망이 아닌 **팀 전멸(Team Wipe)** 시 해당 팀 전체가 리스폰됩니다. 부활 대기 시간(`RespawnDelay`) 동안 대기 후 팀 스폰 지점 근처에 동시 재배치됩니다.
- **클래스 역할:** 각 에이전트는 매 에피소드마다 **Strike / Vanguard / Support** 클래스를 부여받으며, 해당 클래스에 최적화된 포지셔닝 행동을 학습합니다.
- **에피소드 길이:** 고정 스텝 수 기반으로 종료됩니다.
- **스크립티드 AI 대전:** Blue 팀(RL)은 고정 가중치 + 상태 머신으로 구동되는 Red 팀(ScriptedAI)을 상대로 학습합니다. 상대가 고정되어 있으므로 Blue 팀 관점에서 환경이 정상(stationary)으로 유지됩니다.

각 에이전트는 강화학습 정책 네트워크를 통해 실시간 상황에 따라 공간 이동 파라미터를 추론하여 현재 상황에서의 최적 위치를 결정할 수 있습니다.

**ScriptedAI 상세** — Red 팀의 각 에이전트에는 `UDEScriptedAIComponent`가 부착되어 클래스별(Strike/Vanguard/Support) 하드코딩 EQS 가중치 프로파일을 구동합니다. 상태 머신은 4개 상태(Patrol → Approach → Engage → Retreat)로 전투 상황에 반응하며, 에피소드마다 ±0.1 노이즈를 추가해 행동 다양성을 확보합니다. 난이도는 4단계 티어(0=Passive, 1=Basic, 2=Standard, 3=Aggressive)로 구분됩니다.

**커리큘럼 티어 시스템** — `CurriculumScheduler`가 RL 팀의 에피소드별 승률을 모니터링하여 일정 기간 승률이 승격 임계값(Tier 0→1: 50%, 1→2: 55%, 2→3: 65%)을 초과하면 자동으로 다음 티어로 승격합니다. 새 티어는 `scripted_ai_config.json`에 기록되고 UE5의 `LoadTierFromConfig()`가 다음 에피소드 리셋 시 적용합니다. 이를 통해 RL 에이전트가 낮은 난이도에 과적합(overfitting)되지 않고 점진적으로 강한 상대에 노출됩니다.

**승률 기록** — `DECustomCallbacks`가 매 이터레이션 종료 시 `rl_win_rate`, `script_win_rate`, `draw_rate`를 TensorBoard에 기록합니다.

공격(Attack)과 치유(Heal) 어빌리티는 **우선순위 점수(Priority Scoring)** 기반 타겟 선정 정책을 사용합니다. Attack은 `거리 × 0.3 + 낮은 체력 × 0.4 + 클래스 우선순위 × 0.3` 점수로 가장 위협적이거나 취약한 적을 공격하고(Support 클래스 우선), Heal은 `낮은 체력 × 0.7 + 거리 × 0.3` 점수로 가장 위급한 아군을 치유합니다. 이를 통해 어빌리티 행동이 RL 포지셔닝 정책과 자연스럽게 연계됩니다.

Schola 플러그인을 통해 Unreal Engine 5의 강화학습 환경과 외부 스크립트(Ray Rllib)와의 gRPC 기반 브릿지를 구성하였으며 Schola Layer 위에 EQS 가중치를 설정하고 정책 네트워크 출력과 연결하는 Dynamic EQS를 플러그인 형태로 구현하였습니다.

훈련은 초기 하나의 UE 인스턴스에 4개의 병렬 환경으로 수행하였으며 이후 AWS 클라우드 기반의 대규모 병렬 학습 환경을 구축했습니다. 이를 통해 수십 개의 언리얼 엔진 인스턴스로부터 데이터를 동시 수집하고 정책을 업데이트하는 고성능 학습 파이프라인을 구현했습니다.


{{< gif-grid urls="/gifs/project1/1.gif, /gifs/project1/3.gif" widths="50%, 50%" >}}



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


## 주요 기능 (Key Features)


### 1. Dynamic-EQS Plugin

**DynamicEQS**는 Schola 플러그인을 기반으로 하는 **재사용 가능한 UE5 전용 RL-EQS 통합 레이어**입니다. DynamicEQS를 사용하는 게임 프로젝트는 Schola의 저수준 gRPC/ONNX 처리를 직접 다루지 않고 EQS 특화 추상 클래스만 상속하면 됩니다.

DynamicEQS의 주요 클래스는 4가지로, **환경**, **에이전트**, **액션**, **관찰**을 담당합니다. Schola와 게임 프로젝트의 미들 계층에서 EQS 가중치를 설정하고 정책 네트워크와 매핑해주는 역할을 합니다.


{{< img src="/images/project1/pluginarchi.png"
        alt=""
        class="max-w-full"
        caption="Fig 2. 플러그인 계층 구조" >}}

{{< img src="/images/project1/flow2.png"
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


### 2. 관측 공간 및 클래스 조건부 보상 설계 (Class-Conditioned Reward Shaping)


<div style="margin-top: 40px;"></div>

<font size="4">**관측 공간**</font>


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


<div style="margin-top: 40px;"></div>

<font size="4">**보상 구조 개요**</font>


> **스트라이크: 원거리 공격의 높은 데미지**

목표는 원거리에서 높은 데미지를 유지하며 거점을 점령하는 것입니다. 적 거점까지의 거리 감소분에 비례한 접근 보상을 매 스텝 부여하고, 거점 반경 내 진입 시 추가 존재 보너스를 부여합니다. 점령이 완료되면 즉시 `PostCaptureMomentumDuration` 스텝 동안 모멘텀 보너스가 활성화됩니다. 적과 너무 가까운 경우 원거리 역할 이탈 패널티가 부과됩니다.


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

> **뱅가드: 높은 체력, 근접 공격**

목표는 전열에서 근접 전투를 수행하며 거점을 점령하는 것입니다. 적 거점 접근 및 점령 방식은 스트라이크와 동일하게 적용됩니다. 거점 내에서 근접 사거리에 적이 있을 때 추가 근접 보너스(`MeleeRangeBonus`)가 지급되어, 전선을 유지하며 적과 밀착하는 행동을 강화합니다.



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

> **지원: 후방 힐링**

목표는 데미지를 많이 받은 아군을 추적하고 힐링하며 후방을 유지하는 것입니다. 매 스텝 부상 아군 탐색을 수행하되, 잦은 타겟 전환으로 인한 진동 행동을 막기 위해 5스텝 캐시를 적용합니다. 캐시된 아군이 현재 가장 낮은 체력이 아니더라도 5스텝이 지나기 전까지는 교체하지 않습니다. 아군 뒤편에 위치하면 후방 포지셔닝 보너스를 받으며, 아군이 부상 중인 상황에서 직접 킬을 시도하면 역할 이탈 패널티가 부과됩니다.


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

<div style="margin-top: 40px;"></div>

<font size="4">**MAPPO (Multi-Agent PPO) 도입**</font>

본 프로젝트는 **MAPPO(Multi-Agent PPO)** 를 채택하여 역할별 독립 정책(Actor) 3개(`strike_policy`, `vanguard_policy`, `support_policy`)와 공유 중앙집중식 Critic을 함께 학습합니다.

**중앙집중식 Critic (Centralized Critic)** — `CentralizedCritic`은 71-dim 전역 팀 상태(`FDETeamWorldState`: 아군 5명 위치·체력·전략 + 적 5명 위치·신뢰도 + 맵 상태)를 입력받아 팀 전체의 가치를 추정합니다. 개별 에이전트 관측만으로는 알 수 없는 팀-레벨 정보(아군 분포, 전체 거점 점령 상황 등)를 Critic이 직접 참조하므로 Advantage 추정의 분산이 감소합니다.

**Dual Value Estimation** — 각 `EntityCentricRLlibModel`은 로컬 Critic(`V_local`, 226-dim 에이전트 관측)과 중앙 Critic(`V_central`, 71-dim 전역 상태)을 학습 가능한 혼합 계수 α로 결합합니다.

```
V = α · V_local(agent_obs[0:226]) + (1-α) · V_central(global_state[226:297])
```

α는 `sigmoid(_value_mix_logit)`로 초기화되며(초기값 0.5), 학습을 통해 각 역할에 최적인 로컬/전역 비중을 자동으로 결정합니다.

**Self-Attention의 역할** — Actor의 인코더에서 아군·적·거점 각 엔티티 그룹에 Intra-Set Self-Attention을 적용합니다(Zambaldi et al., 2018). 엔티티 토큰들이 서로를 참조하여 "슬롯 3과 슬롯 5가 같은 거점 근처에 집결" 같은 집합 내 공간 관계를 학습합니다. 이 문맥화된 표현이 이후 Cross-Attention의 입력으로 사용됩니다.

**Cross-Attention의 역할** — Self Token(자신 관측 7-dim의 임베딩)이 Query가 되고, Self-Attention을 거친 아군·적·거점 토큰이 Key/Value가 됩니다. Cross-Attention은 "현재 나의 상태에서 각 엔티티가 얼마나 중요한가"를 가중합으로 집약하여 행동(EQS 가중치 7-dim)을 결정합니다.


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



### 3. 평가 모드 (Evaluation Mode)

학습 중 저장된 체크포인트의 성능을 ScriptedAI 대전을 통해 정량적으로 검증하는 **Live Evaluation** 파이프라인(`eval_live.py`)입니다.

<div style="margin-top: 40px;"></div>

<font size="4">**구조 개요**</font>

UE5에 **2개의 서브 환경**을 동시 연결하여 `best` 체크포인트(env 0)와 `latest` 체크포인트(env 1)를 병렬로 평가합니다. 양쪽 모두 Blue 팀(RL)이 Red 팀(ScriptedAI, `bUseScriptedOpponent=true`)과 대전하며, 에피소드 결과를 `win / loss / draw / timeout`으로 분류하고 승률을 산출합니다.

| 항목 | 값 |
|---|---|
| **서브 환경 수** | 2 (best vs latest 동시 평가) |
| **상대** | ScriptedAI (Red, `bUseScriptedOpponent=true`) |
| **기본 평가 에피소드** | 50회 (per checkpoint) |
| **기본 ScriptedAI 티어** | 3 (Aggressive) |
| **결과 출력** | `eval_results_<timestamp>.json` |

<div style="margin-top: 40px;"></div>

<font size="4">**실행 흐름**</font>

1. **ScriptedAI 티어 기록** — 평가 시작 전 `scripted_ai_config.json`에 난이도 티어를 기록하여 UE5의 `LoadTierFromConfig()`가 첫 에피소드 리셋부터 지정 티어로 동작하도록 합니다.
2. **체크포인트 로딩** — `policy_state.pkl`에서 Actor 가중치(`policy.*` prefix)만 추출하여 `EntityCentricPolicy`에 로드합니다. Ray 런타임 없이 CPU에서 즉시 추론 가능한 `eval()` 상태로 전환합니다.
3. **평가 루프** — 에이전트 ID의 `env{i}` prefix로 라우팅하여 두 체크포인트가 독립적으로 행동을 생성합니다. 에피소드 종료 시 UE5의 `WinnerTeamID` 신호를 파싱하여 결과를 기록합니다.
4. **최종 비교** — 목표 에피소드 수 완료 후 승률 비교 결과와 전체 통계를 출력하고 JSON으로 저장합니다.

{{< code lang="python" label="eval_live.py — checkpoint routing" width="100%" height="200px" align="right" >}}
# agent_id prefix "env0_*" → best_policies
# agent_id prefix "env1_*" → latest_policies
env_idx = int(agent_id.split("_")[0][3:])
policy_set = best_policies if env_idx == 0 else latest_policies

# Policy input: first 226 dims of the 297-dim MAPPO obs (agent obs only)
agent_obs = torch.from_numpy(obs[:AGENT_OBS_DIM]).float().unsqueeze(0)  # (1, 226)
action = policy(agent_obs).squeeze(0).numpy()                           # (7,)
{{< /code >}}


<hr style="border: 0; height: 1px; background: #b3b3b3;">


### 4. 학습 결과 (Training Results)

총 2.4M(240만) 타임스텝에 걸쳐 MAPPO 기반 ScriptedAI 대전 학습을 수행했습니다.

{{< img src="/images/project1/reward.png"
        alt=""
        class="max-w-3xl"
        caption="Fig 7. reward" >}}

{{< img src="/images/project1/value.png"
        alt=""
        class="max-w-3xl"
        caption="Fig 8. vf explained, entropy, kl" >}}

{{< img src="/images/project1/loss.png"
        alt=""
        class="max-w-3xl"
        caption="Fig 9. loss" >}}


<div style="margin-top: 40px;"></div>

<font size="4">**핵심 수치 요약**</font>

| 지표 | 값 | 의미 |
|---|---|---|
| **episode_reward_mean** | 0 → 60,000 수렴 | min 보상 동반 상승 — 최악 시나리오에서도 안정적 성능 |
| **vf/explained_var** | 0.87 | Critic이 미래 보상의 87%를 설명 — 높은 상태 가치 예측력 |
| **kl/value** | 낮고 안정 | 정책 업데이트가 신뢰 영역 내에서 제어됨 |
| **entropy** | 0.01 → 0.0005 감소 | 탐험→활용 전환 확인 (스케줄 기반) |
| **losses/vf_loss** | 우상향 | 보상 스케일 증가(0→60K)에 따른 타겟 범위 확대 효과. explained_var 0.87로 Critic 성능은 정상 |

**조기 종료 판단:** lr 스케줄러가 0에 도달하고 reward가 평탄화(plateau)에 진입한 2.4M 스텝에서 학습을 종료했습니다.

<div style="margin-top: 40px;"></div>

<font size="4">**클래스별 학습 결과**</font>

> **Strike**

원거리 거점 점령 역할에 최적화된 정책을 학습했습니다. 적과 최소 교전 거리(`MinCombatRange`)를 유지하면서 목표 거점에 접근하는 행동이 안정적으로 수렴했으며, 거점 점령 완료 후 다음 거점으로 이동하는 모멘텀 패턴이 관측되었습니다.

> **Vanguard**

근접 전열 역할에 맞게 적과 밀착하는 행동을 학습했습니다. 거점 내 근접 사거리(`MeleeRangeBonus`) 조건이 충족되는 포지션을 적극적으로 유지하며, Strike 대비 적에 더 가까운 위치에 수렴하는 EQS 가중치 프로파일이 형성되었습니다.

> **Support**

부상 아군 추적 및 후방 포지셔닝 행동을 학습했습니다. 5스텝 캐시 타겟을 따라 접근하다가 치유 후 다음 타겟으로 전환하는 사이클이 확인되었으며, 아군 뒤편 후방 호 내 체류 시간이 학습 초기 대비 증가했습니다. 부상 아군이 없는 상황에서는 아군 클러스터 중심부에 집결하는 보조 행동이 관측되었습니다.



<hr style="border: 0; height: 1px; background: #b3b3b3;">


### 5. AWS 병렬 학습 환경 (AWS Parallel Training Environment)

로컬 단일 UE5 인스턴스에서 수십 개의 클라우드 병렬 환경으로 확장하는 **AWS 기반 분산 학습 파이프라인**입니다. UE5와 학습 스크립트를 Linux 컨테이너로 패키징하고, Ray Autoscaler를 통해 EC2 클러스터를 동적으로 관리합니다.

<div style="margin-top: 40px;"></div>

<font size="4">**전체 인프라 구성**</font>

{{< img src="/images/project1/aws_architecture.png"
        alt=""
        class="max-w-full"
        caption="Fig 10. AWS 병렬 학습 인프라 전체 구성도 — ECR, EC2 Ray 클러스터(Head/Worker), S3 체크포인트 스토리지, W&B 모니터링의 연결 구조" >}}

학습 인프라는 4개의 주요 컴포넌트로 구성됩니다.

| 컴포넌트 | 역할 |
|---|---|
| **Amazon ECR** | 학습 Docker 이미지 레지스트리 — 모든 노드가 동일 이미지를 Pull하여 환경 일관성 보장 |
| **EC2 Ray 클러스터** | Head(GPU, g4dn.xlarge) + Worker(CPU Spot, c5.2xlarge×4) 혼합 구성 |
| **Amazon S3** | 체크포인트 및 학습 데이터 영구 저장 — s3fs로 노드에 FUSE 마운트 |
| **W&B** | 이터레이션별 보상·승률·손실 지표 실시간 모니터링 |

Terraform(`aws/terraform/main.tf`)으로 VPC, 서브넷, IAM 인스턴스 프로파일, 보안 그룹을 프로비저닝하고, Ray Autoscaler(`aws/cluster.yaml`)가 학습 부하에 따라 Worker 수를 자동 조절합니다.

---

<div style="margin-top: 40px;"></div>

<font size="4">**Ray 클러스터 토폴로지**</font>

{{< img src="/images/project1/aws_cluster.png"
        alt=""
        class="max-w-full"
        caption="Fig 11. Ray 클러스터 노드 구성 — Head 노드(GPU 정책 업데이트)와 Worker 노드(CPU 롤아웃 수집)의 역할 분리 구조" >}}

Head 노드와 Worker 노드는 역할이 명확히 분리됩니다.

| 항목 | Head 노드 | Worker 노드 |
|---|---|---|
| **인스턴스** | g4dn.xlarge (4 vCPU, 16 GB RAM, T4 GPU) | c5.2xlarge (8 vCPU, 16 GB RAM) × 0~4 |
| **구매 옵션** | On-Demand | Spot (비용 최적화) |
| **역할** | Policy gradient 업데이트, 체크포인트 저장 | 병렬 롤아웃 수집 (UE5 env 구동) |
| **GPU** | T4 1개 — PyTorch 추론 가속 | 없음 |

Spot 인스턴스 중단 시 Ray Autoscaler가 자동으로 새 Worker를 기동하여 학습이 중단되지 않습니다. `idle_timeout_minutes: 10` 설정으로 유휴 Worker를 10분 내 자동 종료하여 비용을 절감합니다.

{{< code lang="yaml" label="cluster.yaml — 클러스터 핵심 설정" width="100%" height="250px" align="right" >}}
cluster_name: de-v10-2
max_workers: 4
idle_timeout_minutes: 10

docker:
  image: "<account>.dkr.ecr.us-east-1.amazonaws.com/de:v10-2-latest"
  run_options: ["--shm-size=4g", "--ulimit nofile=65536:65536"]

head_node_type:          # g4dn.xlarge — GPU, on-demand
  InstanceType: g4dn.xlarge
  resources: {GPU: 1}

worker_node_types:       # c5.2xlarge — CPU only, Spot
  - InstanceType: c5.2xlarge
    SpotInstanceType: c5.2xlarge
    resources: {CPU: 8}
{{< /code >}}

---

<div style="margin-top: 40px;"></div>

<font size="4">**Docker 컨테이너 — UE5 Linux 헤드리스 환경**</font>

{{< img src="/images/project1/aws_docker.png"
        alt=""
        class="max-w-full"
        caption="Fig 12. Dockerfile.aws 레이어 구조 — CUDA 베이스, UE5 헤드리스 실행을 위한 가상 디스플레이 설정, Schola gRPC 브릿지, 학습 스크립트 패키징" >}}

`aws/Dockerfile.aws`는 로컬 Dockerfile 대비 클라우드 운영에 특화된 4가지 차이점을 갖습니다.

| 항목 | 내용 |
|---|---|
| **베이스 이미지** | `nvidia/cuda:12.1.1-cudnn8-runtime-ubuntu20.04` — GPU 추론 지원 |
| **UE5 헤드리스** | `ENV DISPLAY=:0` + 가상 프레임버퍼(Xvfb) — 모니터 없이 UE5 실행 |
| **비루트 사용자** | `de` 사용자(UID 1000) 생성 — 컨테이너 보안 강화 |
| **S3 마운트** | `s3fs` + `awscli` 사전 설치 — IAM Role 기반 자격증명(정적 키 없음) |

{{< code lang="dockerfile" label="Dockerfile.aws — 핵심 설정" width="100%" height="250px" align="right" >}}
FROM nvidia/cuda:12.1.1-cudnn8-runtime-ubuntu20.04

# UE5 헤드리스 실행용 가상 디스플레이
ENV DISPLAY=:0

# Ray 메모리 모니터 비활성화 (컨테이너 환경 cgroup 제한 대응)
ENV RAY_DISABLE_MEMORY_MONITOR=1
ENV OMP_NUM_THREADS=1

# 비루트 사용자 — 보안 강화
RUN useradd -u 1000 -g de -m -s /bin/bash de

# S3 마운트용 FUSE 권한 부여
RUN usermod -aG fuse de
{{< /code >}}

---

<div style="margin-top: 40px;"></div>

<font size="4">**학습 파이프라인 흐름**</font>

{{< img src="/images/project1/aws_pipeline.png"
        alt=""
        class="max-w-full"
        caption="Fig 13. AWS 학습 파이프라인 전체 흐름 — Docker 빌드 → ECR Push → Ray 클러스터 기동 → 병렬 롤아웃 수집 → 정책 업데이트 → S3 체크포인트 저장 → W&B 기록" >}}

전체 학습 사이클은 5단계로 진행됩니다.

**1. 이미지 빌드 및 배포**
```bash
docker build -f aws/Dockerfile.aws -t de:v10-2-latest .
docker push <account>.dkr.ecr.us-east-1.amazonaws.com/de:v10-2-latest
```

**2. Ray 클러스터 기동**
```bash
ray up aws/cluster.yaml          # Head + Worker 인스턴스 프로비저닝
ray submit aws/cluster.yaml train_rllib.py -- --config aws/rllib_config.py
```

**3. 병렬 롤아웃 수집**

각 Worker 노드의 Docker 컨테이너 내에서 UE5 헤드리스 인스턴스가 기동되고, Schola gRPC 브릿지를 통해 RLlib 환경(`DECombatEnv-v0`)과 연결됩니다. 4개 Worker가 동시에 롤아웃을 수집하여 `train_batch_size=4096` 배치를 채웁니다.

**4. 정책 업데이트 및 체크포인트**

Head 노드의 GPU에서 PPO gradient 업데이트가 수행되며, 10 이터레이션마다 체크포인트가 S3에 동기화됩니다. `DETrainingCallbacks.on_train_result()`가 신규 최고 승률 달성 시 즉시 `best/` 경로에 추가 동기화합니다.

{{< code lang="python" label="rllib_config.py — S3 체크포인트 동기화" width="100%" height="200px" align="right" >}}
def on_train_result(self, *, algorithm, result, **kwargs):
    wins = result.get("custom_metrics", {}).get("win_mean", 0.0)
    result["custom_metrics"]["win_rate"] = wins

    s3_bucket = os.environ.get("S3_BUCKET", "")
    if s3_bucket and result.get("is_new_best", False):
        checkpoint = algorithm.save()
        _sync_to_s3(checkpoint, s3_bucket, prefix="checkpoints/best/")
{{< /code >}}

**5. 모니터링 (W&B)**

`WandbLoggerCallback`이 매 이터레이션 종료 시 `episode_reward_mean`, `custom_metrics/win_rate`, `policy_reward_mean/{strike,vanguard,support}_policy`, `num_env_steps_sampled_lifetime` 등의 지표를 W&B 대시보드에 기록합니다.

---

<div style="margin-top: 40px;"></div>

<font size="4">**클러스터 종료 및 비용 관리**</font>

```bash
ray down aws/cluster.yaml -y   # 전체 EC2 인스턴스 종료
```

`cache_stopped_nodes: false` 설정으로 `ray down` 시 모든 인스턴스가 즉시 종료(Terminate)되어 유휴 비용이 발생하지 않습니다. S3에 저장된 체크포인트는 클러스터 종료 후에도 유지되므로, 재기동 시 `--restore` 옵션으로 이어서 학습할 수 있습니다.


<hr style="border: 0; height: 1px; background: #b3b3b3;">

## 기술적 난제 및 해결 전략 (Problem Solving)


### Problem 1: 멀티 에이전트 강화학습 환경(Schola + RLlib)에서의 에이전트 개별 사망 처리 결함

{{< img src="/images/project1/problem2.png"
        alt=""
        class="max-w-full"
        caption="Fig 5. 프리징 현상의 원인과 해결" >}}

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

* 관련 문제 Schola OpenSource에 PR 완료.

{{< img src="/images/project1/pr.png"
        alt=""
        class="max-w-3xl"
        caption="Fig 6. Pull Request" >}}


<hr style="border: 0; height: 1px; background: #b3b3b3;">



### Problem 2: 엔티티 간 관계 정보 손실


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

`UAbstractGymConnector::Step()`은 Python과의 전체 한 사이클을 원자적으로 처리하며, 내부에서 `ResolveEnvironmentStateUpdate()`가 gRPC 응답을 블로킹 대기합니다. 따라서 **스텝 호출 빈도를 제어하는 유일한 지점**은 `Step()`을 직접 호출하는 `AGymConnectorManager::Tick()`입니다. 커넥터 내부 구현을 수정하지 않고 오직 `Tick()` 오버라이드만으로 스텝 속도를 외부에서 제어할 수 있다고 생각했습니다.

---

**`ADEGymConnectorManager` 구현**

`AGymConnectorManager`를 상속하는 커스텀 클래스 `ADEGymConnectorManager`를 작성하고, `AGymConnectorManager::Tick()` 대신 `AActor::Tick()`를 직접 호출하여 `Connector->Step()` 호출 빈도를 `StepInterval` 변수로 제어합니다.


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




---

## 결과 (Results)

