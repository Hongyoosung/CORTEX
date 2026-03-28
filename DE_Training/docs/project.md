

본 프로젝트는 **MAPPO(Multi-Agent PPO)** 를 채택하여 역할별 독립 정책(Actor) 3개(`strike_policy`, `vanguard_policy`, `support_policy`)와 공유 중앙집중식 Critic을 함께 학습합니다.

**중앙집중식 Critic (Centralized Critic)** — `CentralizedCritic`은 71-dim 전역 팀 상태(`FDETeamWorldState`: 아군 5명 위치·체력·전략 + 적 5명 위치·신뢰도 + 맵 상태)를 입력받아 팀 전체의 가치를 추정합니다. 개별 에이전트 관측만으로는 알 수 없는 팀-레벨 정보(아군 분포, 전체 거점 점령 상황 등)를 Critic이 직접 참조하므로 Advantage 추정의 분산이 감소합니다.



**Dual Value Estimation** 

각 `EntityCentricRLlibModel`은 로컬 Critic(`V_local`, 226-dim 에이전트 관측)과 중앙 Critic(`V_central`, 71-dim 전역 상태)을 학습 가능한 혼합 계수 α로 결합합니다.


$$V = \alpha \cdot V_{\text{local}}(\text{agent\_obs}[0:226]) + (1 - \alpha) \cdot V_{\text{central}}(\text{global\_state}[226:297])$$


α는 `sigmoid(_value_mix_logit)`로 초기화되며(초기값 0.5), 학습을 통해 각 역할에 최적인 로컬/전역 비중을 자동으로 결정합니다.



**Self-Attention의 역할**

Actor의 인코더에서 아군·적·거점 각 엔티티 그룹에 Intra-Set Self-Attention을 적용합니다(Zambaldi et al., 2018). 엔티티 토큰들이 서로를 참조하여 **"슬롯 3과 슬롯 5가 같은 거점 근처에 집결"** 같은 집합 내 공간 관계를 학습합니다. 이 문맥화된 표현이 이후 Cross-Attention의 입력으로 사용됩니다.



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




### Problem 2: 엔티티 간 관계 정보 손실



학습된 정책이 "적 2명이 같은 거점에 집결" 같은 엔티티 간 공간 패턴을 인식하지 못하는 문제가 관찰되었습니다. 에이전트가 아군이 이미 점령 중인 거점에 중복 배치되는 비효율이 반복되었고, 보상 구조만으로는 이 현상을 충분히 정의하기 어려웠습니다.

---




218-dim 관측 벡터에서 각 엔티티 슬롯은 선형 인코더(`nn.Linear`)를 통해 독립적으로 임베딩됩니다. 이후 Self Token이 Cross-Attention으로 엔티티 집합을 조회하지만, Query가 Self Token 1개이므로 **엔티티 간 상대적 관계**(밀집도, 협공 패턴, 동일 거점 중복)는 Attention weight에 반영되지 않습니다. Cross-Attention의 출력은 "각 엔티티가 Self에게 얼마나 중요한가"의 가중합이지, "엔티티들이 서로 어떤 관계인가"의 정보는 아닙니다.

---


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


---

### Attention 패턴 실증 분석

학습된 체크포인트에서 Attention weight를 직접 추출하여 파이프라인 전체(Self-Attention → Cross-Attention)가 설계 의도대로 동작하는지 검증했습니다.

**실험 설계**

동일한 학습 정책에 두 가지 대조 시나리오를 입력하여 Attention 분포 변화를 관찰했습니다.

| 시나리오 | 설명 |
|---|---|
| **Clustered** | 아군 4명이 동일 거점 반경 내에 밀집 (위치 차이 ≈ 0.01) |
| **Spread** | 아군 4명이 맵 4개 코너에 분산 배치 (위치 차이 ≈ 0.6) |

슬롯 4–7의 Attention weight는 전 역할·전 시나리오에서 0.00으로, 패딩 마스크가 정상 적용되어 유효 엔티티(슬롯 0–3)에만 집중됨을 확인했습니다.

---

#### 1단계 — Intra-Set Self-Attention: 엔티티 간 공간 관계 포착

Self-Attention은 Cross-Attention의 전처리 단계로, 엔티티 토큰들이 서로를 참조해 맥락화된 표현을 생성합니다. 핵심 지표는 **대형 변화(Clustered → Spread)에 따른 가중치 분포의 변동폭(Δmax)** 입니다.

| 역할 | 대형 민감도 | 주요 관찰 |
|---|---|---|
| **Strike** | 중간 (Δmax ≈ 0.19) | Clustered: 4슬롯 균등 분배 → Spread: 슬롯 1(Assault) 집중(0.47) |
| **Support** | 낮음 (Δmax ≈ 0.06) | 대형 무관, 전 슬롯 균등 유지 — 역할 특성과 일치 |
| **Vanguard** | 높음 (Δmax ≈ 0.29) | Clustered: 슬롯 3 집중(0.53) → Spread: 슬롯 1 집중(0.54) |

> *(Figure: `attn_comparison_strike.png` — STRIKE Self-Attention, Clustered vs Spread vs Difference)*

> *(Figure: `attn_comparison_support.png` — SUPPORT Self-Attention, Clustered vs Spread vs Difference)*

> *(Figure: `attn_comparison_vanguard.png` — VANGUARD Self-Attention, Clustered vs Spread vs Difference)*

Support가 가장 낮은 민감도를 보이는 것은 버그가 아닌 **역할 특성의 자연스러운 내재화**입니다. 회복 역할은 팀 대형에 관계없이 전체 아군을 동등하게 모니터링해야 하며, 학습이 이를 반영했습니다. 반면 Vanguard는 가장 높은 민감도를 보여, 전선 앵커 역할이 팀 배치 변화에 가장 민감하게 반응하도록 분화됐음을 나타냅니다.

---

#### 2단계 — Cross-Attention: 행동 직전 최종 정보 집약

Cross-Attention은 Self Token(자신의 관측 임베딩)이 Query가 되어 아군·적·거점 세 엔티티 집합 각각을 조회하는 단계입니다. 이 가중합이 직접 EQS 가중치(행동)로 이어지므로, "에이전트가 실제 행동 결정 시 무엇을 보는가"를 가장 직접적으로 드러냅니다.

**적(Enemy) Cross-Attention**

전 역할에서 활성 적 슬롯 0·1에 가중치가 거의 균등하게 분배됩니다(≈ 0.48–0.52). 두 적을 동등한 위협으로 인식하는 일관된 패턴으로, 슬롯 2–7(패딩)은 정확히 0으로 억제됩니다.

**거점(Base) Cross-Attention**

역할마다 우선 거점이 명확히 다릅니다. Strike와 Support는 거점 0·1에 집중(≈ 0.40–0.42)하는 반면, Vanguard는 거점 1·2에 더 분산된 가중치를 보입니다. 전선 유지 역할인 Vanguard가 중립 거점 및 후방 거점을 균형 있게 참조하도록 분화된 결과로 해석됩니다.

**아군(Ally) Cross-Attention — Self-Attention과의 일관성 검증**

> *(Figure: `attn_cross_strike_clustered.png` / `attn_cross_strike_spread.png` — STRIKE Cross-Attention)*

> *(Figure: `attn_cross_vanguard_clustered.png` / `attn_cross_vanguard_spread.png` — VANGUARD Cross-Attention)*

> *(Figure: `attn_cross_support_clustered.png` / `attn_cross_support_spread.png` — SUPPORT Cross-Attention)*

Vanguard의 Cross-Attention 아군 가중치는 Self-Attention 결과와 정확히 대응합니다. Clustered에서 Self-Attention이 슬롯 3을 지배적으로 선택했고, Cross-Attention도 슬롯 3을 가장 높게 참조합니다(0.50). Spread에서는 양쪽 모두 슬롯 1로 초점이 이동합니다(Self: 0.54, Cross: 0.42). 이는 Self-Attention이 생성한 맥락화된 표현을 Cross-Attention이 일관성 있게 활용함을 보여주며, **두 단계 파이프라인이 의도대로 연결되어 작동함을 실증합니다.**

---

**종합**

| 검증 항목 | 결과 |
|---|---|
| 패딩 마스크 억제 | 슬롯 4–7 완전 억제 — 정상 |
| 역할별 Self-Attention 분화 | Support(균등) / Strike(중간) / Vanguard(고민감) — 역할 특성과 일치 |
| Cross-Attention 엔티티 우선순위 | 적: 균등 위협 인식 / 거점: 역할별 상이 / 아군: Self-Attention과 일관 |
| 파이프라인 일관성 | Self → Cross Attention 간 초점 대상 일치 — 파이프라인 정합성 확인 |

보상 함수나 역할 레이블 외 별도의 귀납 편향 없이, Intra-Set Self-Attention 구조만으로 **역할 특화된 공간 추론의 자발적 분화**가 달성됐음을 두 단계의 Attention 패턴을 통해 실증했습니다.