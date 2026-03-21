# Self-Attention Ablation 비교 가이드

## 목적
Relational Self-Attention 도입 전후의 정량적 차이를 측정하여 포트폴리오에 기재할 수치를 확보합니다.

---

## 1. 현재 상태: 토글 불가

`policy.py`의 `EntityCentricPolicy`에 Self-Attention이 하드코딩되어 있어 on/off 토글이 없습니다.
**Ablation을 위해 별도 클래스를 만들어야 합니다.**

---

## 2. Ablation 변형 구현 (policy.py에 추가)

```python
class EntityCentricPolicy_NoSelfAttn(nn.Module):
    """Self-Attention을 제거한 Ablation 변형. Cross-Attention만 사용."""

    def __init__(self, hidden: int = 64, heads: int = 4):
        super().__init__()
        self.hidden = hidden

        # 동일한 인코더
        self.self_enc  = nn.Linear(SELF_DIM, hidden)
        self.ally_enc  = nn.Linear(ALLY_DIM, hidden)
        self.enemy_enc = nn.Linear(ENEMY_DIM, hidden)
        self.base_enc  = nn.Linear(BASE_DIM, hidden)

        # Self-Attention 없음 — Cross-Attention만 유지
        self.ally_attn  = nn.MultiheadAttention(hidden, heads, batch_first=True)
        self.enemy_attn = nn.MultiheadAttention(hidden, heads, batch_first=True)
        self.base_attn  = nn.MultiheadAttention(hidden, heads, batch_first=True)

        combined_dim = hidden * 4
        self.action_head = nn.Sequential(
            nn.Linear(combined_dim, 256), nn.ReLU(),
            nn.Linear(256, 128), nn.ReLU(),
            nn.Linear(128, EQS_DIM), nn.Tanh(),
        )
        self.value_head = nn.Sequential(
            nn.Linear(combined_dim, 256), nn.ReLU(),
            nn.Linear(256, 1),
        )
        self.log_std = nn.Parameter(torch.zeros(EQS_DIM) - 1.5)

    def _encode(self, flat):
        self_obs, allies, enemies, bases, ally_mask, enemy_mask, base_mask, _ = self._unpack(flat)
        s = self.self_enc(self_obs)
        q = s.unsqueeze(1)

        a_enc = self.ally_enc(allies)
        e_enc = self.enemy_enc(enemies)
        b_enc = self.base_enc(bases)

        # Self-Attention 스킵 — 바로 Cross-Attention
        a_ctx, _ = self.ally_attn(q, a_enc, a_enc, key_padding_mask=ally_mask)
        e_ctx, _ = self.enemy_attn(q, e_enc, e_enc, key_padding_mask=enemy_mask)
        b_ctx, _ = self.base_attn(q, b_enc, b_enc, key_padding_mask=base_mask)

        return torch.cat([s, a_ctx.squeeze(1), e_ctx.squeeze(1), b_ctx.squeeze(1)], dim=-1)

    # forward, get_value, sample_action 등은 EntityCentricPolicy와 동일
```

---

## 3. 실험 방법

### 방법 A: UE5 시뮬레이션 기반 (가장 신뢰도 높음)

두 모델을 각각 동일 조건으로 UE5 환경에서 학습시키고 비교합니다.

**절차:**
1. `train.py`에서 `EntityCentricRLlibModel`의 내부 정책을 교체할 수 있도록 환경 변수 추가:
   ```python
   USE_SELF_ATTN = os.environ.get('USE_SELF_ATTN', '1') == '1'
   # __init__에서:
   if USE_SELF_ATTN:
       self.policy = EntityCentricPolicy(hidden=hidden, heads=heads)
   else:
       self.policy = EntityCentricPolicy_NoSelfAttn(hidden=hidden, heads=heads)
   ```
2. 두 번 학습 실행:
   - `USE_SELF_ATTN=1 python train.py --mode rllib --iterations 100`
   - `USE_SELF_ATTN=0 python train.py --mode rllib --iterations 100`
3. TensorBoard에서 비교

**비용:** UE5 인스턴스 2회 학습 필요 (각 2.4M 스텝 × 수 시간)

---

### 방법 B: 오프라인 합성 데이터 비교 (UE5 불필요, 빠른 검증)

UE5를 구동하지 않고, 기존 학습 데이터 또는 합성 관측 데이터로 두 모델의 **표현력 차이**만 비교합니다. 학습 성능의 직접 비교는 아니지만, "Self-Attention이 관계 정보를 포착하는가"를 검증할 수 있습니다.

**절차:**

```python
# ablation_test.py — UE5 없이 실행 가능
import torch
from policy import EntityCentricPolicy, OBS_DIM

# 1. 두 모델 생성
model_sa = EntityCentricPolicy(hidden=64, heads=4)       # Self-Attention 있음
model_no = EntityCentricPolicy_NoSelfAttn(hidden=64, heads=4)  # 없음

# 2. 합성 시나리오: 아군 2명이 같은 위치 vs 분산 배치
obs_clustered = torch.zeros(1, OBS_DIM)
obs_spread    = torch.zeros(1, OBS_DIM)

# Self 토큰 (동일)
obs_clustered[0, 0:3] = torch.tensor([0.5, 0.5, 0.5])  # 위치
obs_clustered[0, 6]   = 1.0                              # 체력

obs_spread[0, 0:7] = obs_clustered[0, 0:7]

# 아군 슬롯 0, 1: 같은 위치 (clustered)
for i in range(2):
    start = 7 + i * 8
    obs_clustered[0, start:start+3] = torch.tensor([0.1, 0.1, 0.0])  # 같은 상대 위치
    obs_clustered[0, start+3] = 1.0   # health
    obs_clustered[0, start+4] = 1.0   # alive

# 아군 슬롯 0, 1: 다른 위치 (spread)
obs_spread[0, 7:7+3]   = torch.tensor([0.1, 0.1, 0.0])
obs_spread[0, 7+3]     = 1.0
obs_spread[0, 7+4]     = 1.0
obs_spread[0, 15:15+3] = torch.tensor([-0.3, 0.2, 0.0])  # 다른 위치
obs_spread[0, 15+3]    = 1.0
obs_spread[0, 15+4]    = 1.0

# 나머지 슬롯 패딩
obs_clustered[0, 167:175] = torch.tensor([0,0,1,1,1,1,1,1], dtype=torch.float)  # 아군 0,1만 유효
obs_spread[0, 167:175]    = torch.tensor([0,0,1,1,1,1,1,1], dtype=torch.float)
obs_clustered[0, 175:183] = 1.0  # 적 전부 패딩
obs_spread[0, 175:183]    = 1.0
obs_clustered[0, 183:191] = 1.0  # 거점 전부 패딩
obs_spread[0, 183:191]    = 1.0

# 3. EQS 출력 비교
with torch.no_grad():
    sa_clustered = model_sa(obs_clustered)
    sa_spread    = model_sa(obs_spread)
    no_clustered = model_no(obs_clustered)
    no_spread    = model_no(obs_spread)

# Self-Attention 모델은 clustered vs spread에서 더 큰 출력 차이를 보여야 함
sa_diff = (sa_clustered - sa_spread).abs().mean().item()
no_diff = (no_clustered - no_spread).abs().mean().item()

print(f"Self-Attention 모델: clustered vs spread 출력 차이 = {sa_diff:.4f}")
print(f"No-Self-Attn 모델:  clustered vs spread 출력 차이 = {no_diff:.4f}")
print(f"비율: {sa_diff / (no_diff + 1e-8):.2f}x")
```

**주의:** 이 테스트는 **초기화 직후 랜덤 가중치** 상태에서의 표현력 차이만 보여줍니다. 학습된 모델에서 비교하려면 체크포인트를 로드해야 합니다.

---

### 방법 C: 기존 체크포인트의 Attention Weight 시각화

학습 완료된 Self-Attention 모델의 체크포인트가 있다면, Attention weight를 추출하여 "어떤 엔티티 쌍에 높은 attention을 부여하는가"를 시각화할 수 있습니다.

```python
# attention_viz.py
model = EntityCentricPolicy()
model.load_state_dict(torch.load("checkpoint.pt"))
model.eval()

# hook으로 attention weight 추출
attn_weights = {}
def hook_fn(name):
    def hook(module, input, output):
        attn_weights[name] = output[1]  # (B, 8, 8) attention matrix
    return hook

model.ally_self_attn.register_forward_hook(hook_fn("ally_self"))
model.enemy_self_attn.register_forward_hook(hook_fn("enemy_self"))
model.base_self_attn.register_forward_hook(hook_fn("base_self"))

# 관측 데이터로 forward pass
with torch.no_grad():
    model(obs)

# attn_weights["ally_self"] 를 heatmap으로 시각화
# → 같은 거점 근처 아군끼리 높은 attention weight를 보이는지 확인
```

---

## 4. 비교할 핵심 수치

### A. 학습 성능 (UE5 시뮬레이션 필요)

| 지표 | 확인 위치 | 기대 |
|---|---|---|
| `episode_reward_mean` | TensorBoard | Self-Attn 모델이 더 빠르게 수렴하거나 더 높은 plateau |
| `episode_reward_min` | TensorBoard | Self-Attn 모델의 worst-case 보상이 더 높음 |
| `CoOccupation 빈도` | UE5 로그 / 커스텀 메트릭 | Self-Attn 모델에서 거점 중복 배치 빈도 감소 |
| `거점 점령 수` | UE5 GameMode 통계 | 더 효율적인 거점 분배 → 더 높은 점령 수 |

### B. 표현력 (오프라인 가능)

| 지표 | 확인 위치 | 기대 |
|---|---|---|
| 밀집/분산 시나리오 EQS 출력 차이 | ablation_test.py | Self-Attn이 더 민감하게 반응 |
| Attention weight 분포 | attention_viz.py | 같은 거점 근처 엔티티 쌍에 높은 weight |
| 파라미터 수 | `sum(p.numel())` | 168K vs 268K (+60%) |
| 추론 레이턴시 | `torch.cuda.Event` 타이밍 | < 2ms 양쪽 모두 만족하는지 |

---

## 5. 체크포인트 위치

| 항목 | 경로 |
|---|---|
| RLlib 체크포인트 | `DE_Training/training_results/*/checkpoint_*` |
| 수동 저장 .pt | `train.py --mode eval --checkpoint <path>` 로 로드 가능 |
| TensorBoard 로그 | `DE_Training/training_results/*/` |

---

## 6. 권장 실행 순서

1. **즉시 실행 가능 (방법 B):** `EntityCentricPolicy_NoSelfAttn` 클래스를 policy.py에 추가하고, ablation_test.py를 실행하여 표현력 차이를 확인합니다. UE5 불필요.

2. **체크포인트가 있다면 (방법 C):** 학습된 모델의 attention weight를 시각화하여 "Self-Attention이 실제로 의미 있는 관계를 포착하는가"를 정성적으로 확인합니다.

3. **시간이 허락하면 (방법 A):** 두 모델을 동일 조건으로 UE5에서 학습시키고 TensorBoard 비교. 포트폴리오에 기재할 가장 설득력 있는 수치를 얻을 수 있습니다.
