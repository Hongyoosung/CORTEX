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
| **Cloud & Infra** | AWS (EC2, EKS), Docker (Linux) |
| **Communication** | gRPC (Schola protocol) |
| **Monitoring** | TensorBoard |


---


## 주요 기능 (Key Features)


### 1. RL & EQS 통합 로직 (Actuator Transformation)
강화학습 모델이 에이전트를 직접 움직이는 대신, 가중치(Weights) 를 출력하면 UE5의 환경 쿼리 시스템(EQS)이 이를 바탕으로 물리적 공간을 해석합니다.

RL 정책의 출력 공간은 Box([-1, 1]^6)이며, TacticalParameterActuator를 통해 다음 6개의 공간적 의미로 매핑됩니다.

| 파라미터 | 설명 |
| --- | --- |
| **EnemyObjectiveProximity** | 적 거점 접근 선호도 |
| **AllyObjectiveProximity** | 아군 거점 접근 선호도 |
| **CoverDensity** | 지형지물 엄폐 선호도 |
| **EnemyVisibility** | 적 가시성 (교전 혹은 회피) 선호도 |
| **AllyProximity** | 아군과의 진형 유지 선호도 |
| **CombatRange** | 최적의 무기 사거리 유지 선호도 |


**RL 정책의 출력 -> UE5 액추에이터 변환 로직**

```C++
// Schola Actuator가 Python의 Action Tensor를 UE5 EQS 파라미터로 디코딩
void UTacticalParameterActuator::TakeAction(const FBoxPoint& Action)
{
    // Action.Values: RLlib에서 도출된 6차원 Float 배열 [-1.0, 1.0]
    FEQSWeightParameters Weights;
    Weights.EnemyObjectiveProximity = Action.Values[0];
    Weights.AllyObjectiveProximity = Action.Values[1];
    Weights.CoverDensity = Action.Values[2];
    Weights.EnemyVisibility = Action.Values[3];
    Weights.AllyProximity = Action.Values[4];
    Weights.CombatRange = Action.Values[5];

    // 가중치 유효성 검사 및 클램핑
    if (!ValidateEQSWeights(Weights)) {
        Weights.Clamp();
    }

    // 에이전트의 블랙보드 업데이트 및 내비게이션 경로 재탐색 트리거
    if (MocAgent) {
        MocAgent->UpdateTacticalWeights(Weights); 
        MocAgent->PerformTacticalAction(); 
    }
}
```

이러한 아키텍처로 RL은 '어떤 전술이 유리한가(가중치)'라는 추상적 의사결정에 집중할 수 있었고, 복잡한 3D 환경에서의 물리적 충돌 처리나 경로 탐색 비용을 획기적으로 낮췄습니다.



---


### 2. 관측 공간 및 전략 조건부 보상 설계 (Strategy-Conditioned Reward Shaping)

**관측 공간**

| 데이터 구분 | 세부 항목 | | 설명 및 처리 방식 | 차원 |
| --- | --- | --- | --- | --- |
| **Self** | 위치, 체력, 속도 | 정규화된 맵 위치 및 에이전트의 현재 상태 | 7 |
| **Allies** | 상대 위치, 체력 | 최대 4명. 거리/시야 정규화 | 16 |
| **Enemies** | 적군 상대 위치, 시야 플래그 | 시권(Line of Sight) 내 적만 유효 좌표 제공 | | 20
| **Map** | 5개 거점 소유권 | 아군(+1), 중립(0), 적군(-1) 상태 플래그 | 5 |
| **Strategy** | 현재 부여된 전술 역할 | 돌격/방어/지원 |  One-hot 인코딩 벡터 | 3 |
| **합계** | | | | 51 |


<br>

**보상 구조 개요**

단일 보상 함수로 세 가지 역할을 동시에 학습시키면 그래디언트 간섭(Gradient Interference)이 발생합니다. 한 역할에 유리한 업데이트가 다른 역할의 정책을 손상시키기 때문입니다. 이를 해결하기 위해, 역할별로 완전히 독립된 정책 네트워크와 전술 목표에 특화된 조밀(Dense) + 희소(Sparse) 보상 함수 조합을 설계했습니다.

모든 전략에 공통으로 적용되는 베이스라인 보상 위에, 역할별 전술 목표에 맞춘 밀도 보상(Dense)이 매 스텝마다 계산됩니다. 킬/점령 등 이산 이벤트는 희소 보상(Sparse)으로 별도 누적되어 스텝 종료 시 함께 드레인됩니다.

```cpp
// DERewardSubsystem.cpp — 전략별 스케일 분기 구조
float UDERewardSubsystem::GetStrategyScale(
    EDEStrategyType Strategy,
    float AssaultScale, float DefendScale, float SupportScale) const
{
    switch (Strategy)
    {
    case EDEStrategyType::Assault: return AssaultScale;
    case EDEStrategyType::Defend:  return DefendScale;
    case EDEStrategyType::Support: return SupportScale;
    }
}
```

킬·사망·점령 등 모든 이벤트 보상은 이 스케일을 통해 역할마다 다른 가중치로 적용됩니다. 예를 들어 킬 보상은 돌격에 높고 지원에 낮으며, 사망 패널티는 방어에 더 크게 부과됩니다.




---

> **돌격: 거점의 확보**

목표는 적 거점 접근과 점령 완료입니다. 적 거점까지의 거리 감소분에 비례한 접근 보상을 매 스텝 부여하고, 거점 반경 내 진입 시 추가 존재 보너스를 부여합니다. 점령이 완료되면 즉시 `PostCaptureMomentumDuration` 스텝 동안 모멘텀 보너스가 활성화되어, 점령 후 제자리에 머무는 대신 다음 거점으로 계속 전진하도록 유도합니다.

```cpp
// 점령 완료 → 모멘텀 활성화
if (NewCaptures > 0)
    InOutState.PostCaptureMomentumStepsRemaining = Settings->AssaultReward.PostCaptureMomentumDuration;

// 모멘텀 기간 중 이동 + 점령 거점에서 벗어난 경우 보너스 부여
if (InOutState.PostCaptureMomentumStepsRemaining > 0)
{
    InOutState.PostCaptureMomentumStepsRemaining--;
    if (PositionChange >= Settings->AssaultReward.PostCaptureMomentumMinMove &&
        FVector::DistSquared(Current.Position, InOutState.LastCapturedPointLocation) > CaptureRadiusSq)
    {
        Reward += Settings->AssaultReward.PostCaptureMomentumBonus;
    }
}

// 비전투 구역에서 정지 시 패널티
if (PositionChange < Settings->AssaultIdleMovementThreshold && !bInNonFriendlyZone)
    Reward -= IdlePenalty;
```

---

> **방어: 거점의 유지**

목표는 아군 거점 유지와 거점 내 적 격퇴입니다. 아군 거점 반경 내에 위치할 때 기본 존재 보상이 부여됩니다. 거점 내에서 적에게 데미지를 받으면 추가 내구도 보너스(`ZoneDurabilityBonus`)가 지급되어, 거점에서 물러나지 않고 버티는 행동을 강화합니다. 아군 거점이 없는 상황에서는 중립/적 거점 접근으로 목표가 전환됩니다.

```cpp
// 거점 내 위치 시 기본 보상 + 체력·정지 보너스
if (InOutState.bInFriendlyZone)
{
    Reward += Settings->DefendReward.ZonePresenceBonus;
    if (PositionChange < Settings->DefendStationaryThreshold) Reward += Settings->DefendReward.PositionReward;
    if (Current.Health > Settings->DefendHealthThreshold)     Reward += Settings->DefendReward.HealthBonus;

    // 거점 내 적이 탐지된 경우 위협 대응 보너스
    // (적이 거점 반경 내에 있을 때 내가 거점 안에 있으면 추가 보상)
    Reward += Settings->DefendReward.ThreatResponseBonus; // 조건 충족 시

    // 거점 내에서 데미지를 흡수한 경우 내구도 보너스
    const float DamageTaken = Prev.Health - Current.Health;
    if (DamageTaken > 0.0f)
        Reward += Settings->DefendReward.ZoneDurabilityBonus * DamageTaken;
}
// 거점 외부에 있는 경우 거리에 비례한 패널티
else
{
    const float DistPenalty = FMath::Min(CurrNearestFriendlyDist / 10000.0f, 1.0f) * 0.3f;
    Reward -= DistPenalty;
}
```

---

> **지원: 아군의 유지**

목표는 체력이 낮은 아군을 추적하고 힐링하며 후방을 유지하는 것입니다. 매 스텝 부상 아군 탐색을 수행하되, 잦은 타겟 전환으로 인한 진동 행동을 막기 위해 5스텝 캐시를 적용합니다. 캐시된 아군이 현재 가장 낮은 체력이 아니더라도 5스텝이 지나기 전까지는 교체하지 않습니다. 아군 뒤편에 위치하면 후방 포지셔닝 보너스를 받으며, 아군이 부상 중인 상황에서 직접 킬을 시도하면 역할 이탈 패널티가 부과됩니다.

```cpp
// 5-step 캐시: 타겟 아군 교체를 억제하여 안정적 추적 유도
bool bShouldReevalTarget =
    (InOutState.CachedInjuredAllyIdx < 0) ||
    (InOutState.InjuredAllyStalenessCounter >= 5) || // 5스텝마다 재평가
    (/*캐시된 아군이 사망한 경우*/);

if (bShouldReevalTarget)
{
    // 생존 아군 중 체력이 가장 낮은 대상을 새 타겟으로 지정
    InOutState.CachedInjuredAllyIdx = NewInjuredIdx;
    InOutState.InjuredAllyStalenessCounter = 0;
}

// 타겟 아군에 접근할수록 매 스텝 보상 누적
Reward += Settings->SupportReward.AllyApproachReward * FMath::Max(ApproachDelta, 0.0f);

// 아군이 부상 중인데 킬을 시도한 경우 역할 이탈 패널티
if (InOutState.bSparseKillFiredThisStep && bAllyInjured)
    Reward -= Settings->SupportReward.RoleBreakPenalty;

// 아군보다 적에서 먼 위치를 유지하는 경우 후방 포지셔닝 보너스
if (NearestEnemyDist > NearestAllyToEnemyDist)
    Reward += Settings->SupportReward.RearGuardBonus;
```

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
# moc_v10_2_env.py — 워커별 포트 자동 배정
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

### 4. 듀얼 모드 아키텍처 (Dual-Mode Architecture)

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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MOC")
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
| **에피소드 관리** | `ADEScholaEnvironment`가 제어 | 불필요 (게임 루프로 동작) |
| **환경 리셋** | Schola 프로토콜 기반 | 없음 |

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
### Problem 2: 학습 환경 병렬화에 따른 환경 불안정 문제

Windows 환경에서 Ray의 멀티 워커 아키텍처를 구동할 때 두 가지 문제가 발생했습니다. (1) 가중치 동기화 단계에서 Ray Learner 액터가 멈추는 현상, (2) 단일 UE5 인스턴스에 모든 워커가 연결을 시도하여 처리량이 병목되는 문제였습니다.

#### Goal
UE5와의 안정적인 통신을 유지하면서, OS 의존성 없이 수평 확장 가능한 멀티 워커 학습 파이프라인 구축.

#### Solution

**Docker 컨테이너화**: Python 학습 스크립트와 Ray RLlib 의존성 전체를 Linux Docker 이미지로 패키징했습니다. Windows의 `spawn`/`fork` 프로세스 생성 방식이 Ray와 충돌하던 문제를 컨테이너 레이어에서 원천적으로 차단했습니다.

**gRPC 포트 라우팅 최적화**: Schola의 연결 초기화 과정을 커스텀하여, 각 RLlib env-runner가 `base_port + worker_index` 공식으로 고유한 포트를 계산해 서로 다른 UE5 인스턴스에 접속하도록 했습니다.

```python
# moc_v10_2_env.py — Schola 연결 초기화
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

### Problem 3: 멀티 에이전트 에피소드 경계에서의 학습 프리징(정지) 현상

학습 도중 에피소드가 끝나는 경계 시점에서 시스템 전체가 멈추는 현상이 반복적으로 발생했습니다. 생존한 에이전트는 새로운 가중치 업데이트를 받지 못하고 정지했고, 이미 사망한 에이전트는 '사망 → 자동 부활 → 즉사' 사이클을 무한 반복하고 있었습니다.

#### Goal
에피소드 경계에서의 프리징 현상 해결 및 안정적인 멀티 에이전트 에피소드 종료 구현.

#### 근본 원인 분석: 두 종료 시스템의 충돌

Schola 측(`AutoResetType::SAME_STEP`)과 Python 측(RLlib) 사이에 에피소드 종료 신호가 서로 모순되는 상태였습니다.

- **Schola 측**: 에이전트가 사망하면 `SAME_STEP` 정책에 의해 즉시 자동 리셋을 트리거했습니다.
- **Python 측**: RLlib은 혼합 궤적(서로 다른 에피소드의 데이터가 한 배치에 섞이는 것)이 생기지 않도록, 모든 에이전트의 종료 신호(`done`)를 억제하고 있었습니다.

결과적으로 사망한 에이전트는 Schola가 부활시키자마자 다시 사망하는 무한 루프에 빠지고, 그 루프가 Schola의 스텝 예산(step budget) 전체를 소비해버렸습니다. 생존한 에이전트들은 스텝 버짓이 고갈된 Schola의 멀티에이전트 동기화 장벽(step barrier)에 막혀 영원히 다음 액션을 받지 못하는 상태가 되었습니다.

```
[수정 전]
생존 에이전트 │──────────── 대기 중 (프리징) ────────────────────────────>
사망 에이전트 │사망→리셋→사망→리셋→사망→리셋→ (스텝 버짓 소진) ─────>
                                          ↑ 모든 스텝을 여기서 낭비

[수정 후]
생존 에이전트 │─────────────── 정상 스텝 진행 ──────────── MaxStep → 종료
사망 에이전트 │사망 → Running 유지 (DEMatchManager 팀 부활 대기) → MaxStep → 종료
```

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


## 결과 (Results)

### 학습 곡선 분석 (Training Curve Analysis)

본 프로젝트는 **셀프플레이(Self-Play)** 방식으로 학습을 진행했습니다. 고정된 규칙 기반 AI를 상대하는 것이 아니라, 양 팀이 서로의 전략에 반응하며 함께 진화하는 구조입니다. 이는 기술적으로 훨씬 높은 난이도를 요구하는데, 상대방이 고정된 목표(fixed target)가 아니라 끊임없이 변화하는 이동 목표(moving target)이기 때문입니다.

학습 총 스텝은 **120만 스텝**이며, 약 **80만 스텝** 시점에서 최적 성능에 도달했습니다.

{{< img src="/images/project1/tb_reward.png"
        alt="Reward"
        class="max-w-full"
        caption="Fig 2. 에피소드 평균 보상 (Episode Mean Reward)" >}}

{{< img src="/images/project1/tb_entropy.png"
        alt="Entropy"
        class="max-w-full"
        caption="Fig 3. 정책 엔트로피 (Policy Entropy)" >}}

{{< img src="/images/project1/tb_vf_explained.png"
        alt="VF Explained Variance"
        class="max-w-full"
        caption="Fig 4. 가치 함수 설명 분산 (VF Explained Variance)" >}}

{{< img src="/images/project1/tb_kl.png"
        alt="KL Divergence"
        class="max-w-full"
        caption="Fig 5. KL 발산 (Approx. KL Divergence)" >}}

{{< img src="/images/project1/tb_policy_loss.png"
        alt="Policy Loss"
        class="max-w-full"
        caption="Fig 6. 정책 손실 (Policy Loss)" >}}

<br>

#### 보상 그래프의 해석: 셀프플레이에서 보상이 증가하는 이유

셀프플레이 제로섬 게임에서는 한 팀이 얻으면 다른 팀이 잃으므로, 단순 승패 보상만 사용했다면 평균 보상이 0으로 수렴해야 합니다. 그러나 본 그래프에서는 보상이 전반적으로 **우상향** 추세를 보입니다.

이는 설계된 **밀도 보상(Dense Reward) 구조** 덕분입니다. 단순 승패(희소 보상) 외에도 거점 점령, 아군 거점 방어, 힐링 효율, 전술 역할 준수 등 **에이전트의 전술적 성장을 유도하는 다양한 중간 보상**을 매 스텝마다 부여했습니다. 그 결과 양 팀이 서로를 상쇄하는 구조 속에서도, 두 팀 모두 거점을 더 효율적으로 점령하고 역할에 충실하게 행동하는 방향으로 공진화(co-evolution)하면서 전체 보상의 절대값이 상승하게 됩니다.

<br>

#### 최적 체크포인트 선택: 80만 스텝

80만 스텝 이후 **전략 붕괴(Strategy Collapse)** 현상이 관찰되었습니다. 셀프플레이의 특성상 에이전트들이 수치적으로 가장 유리한 패턴만을 반복하게 되는 과최적화가 발생한 것입니다.

이는 두 지표에서 명확하게 나타납니다.

- **VF Explained Variance 하락:** 80만 스텝 이후 Explained Variance가 감소하기 시작합니다. 이는 가치 함수가 고정된 상대의 패턴에 과적합(overfitting)되면서, 새로운 상황에 대한 가치 판단의 범용성이 낮아졌음을 의미합니다.
- **Entropy 감소:** 정책의 엔트로피가 지속적으로 낮아지며 행동의 다양성이 줄어들었습니다. 다양한 전술적 대응보다 특정 패턴에 수렴하기 시작한 신호입니다.

따라서 전술적 대응 패턴이 가장 다양하고 범용성이 높았던 **80만 스텝 모델을 최종 모델로 선택**했습니다.
