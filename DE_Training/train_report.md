---
# PPO 학습 종합 분석 보고서
**실험 ID:** `20260326_003733` | **현재 스텝:** 2,101,945 | **경과 시간:** 5.747시간

***
## 실험 개요
3개의 멀티에이전트(Strike, Support, Vanguard)를 PPO로 동시 학습한 결과로, 총 약 210만 스텝에 걸친 학습이 진행되었습니다. Learning Rate는 세 에이전트 모두 cosine/linear decay로 현재 `1e-4` 수준까지 감소한 상태입니다. [ppl-ai-file-upload.s3.amazonaws](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/images/72742881/e14e6bc4-dea9-48c0-9bc8-b0d36517fdfe/image-5.jpg)

***
## Episode Reward 분석
| 에이전트 | Min | Mean | Max | 평가 |
|---|---|---|---|---|
| Strike | 13.88 | 24.88 | 50.26 | ✅ 안정적 상승 |
| Support | **-3.77** | 13.98 | 64.96 | ⚠️ 음수 Min 존재 |
| Vanguard | 17.06 | 46.37 | **137.83** | ✅ 압도적 우위 |

Vanguard는 Mean 46.37, Max 137.83으로 세 에이전트 중 가장 높은 보상을 기록 중이며, reward 곡선이 2.1M 스텝에서도 우상향 중입니다. Support는 유일하게 episode_min이 음수(-3.77)로, 일부 에피소드에서 완전히 실패하는 케이스가 존재합니다.

***
## 학습 안정성 종합
레이더 차트 기준으로 Vanguard는 보상 평균과 최대 잠재력에서 압도적이나, Entropy 감소(-0.64)가 가장 심해 탐색 능력이 줄어들고 있는 점이 유일한 약점입니다. Strike는 5개 항목에서 가장 균형 잡힌 모습을 보이며, Support는 Reward 안정성이 낮은 것이 주요 과제입니다. 

***
## KL Divergence 분석
세 에이전트 모두 KL divergence가 0.006~0.007 수준으로 안정적입니다. PPO의 clip 범위 내에서 정책이 업데이트되고 있으며, 발산 위험은 없습니다.

***
## Explained Variance & Entropy
Value function의 explained_variance는 Strike 0.9465, Support 0.9405, Vanguard 0.9684로 모두 0.94 이상을 기록 중입니다. 이는 critic이 future return을 매우 정확하게 예측하고 있음을 의미하며, 학습이 올바르게 수렴하고 있다는 신호입니다. Entropy는 세 에이전트 모두 감소 추세이며, 특히 Vanguard(-0.64)와 Strike(-0.60)가 빠르게 수렴 중입니다. 
***

## 최종 권고사항
**결론: 계속 진행 권장 (목표: 3M Steps)**

- ✅ **Strike / Vanguard** → 현재 하이퍼파라미터 유지하며 3M까지 진행
- ⚠️ **Support** → reward_min이 지속 음수라면 **penalty 조건 또는 reward shaping 재검토** 필요
- ⚠️ **Entropy 모니터링** → Vanguard/Strike의 entropy가 -0.7 이하로 내려가면 `ent_coef` 소폭 증가 고려 (예: 0.005 → 0.01)
- ℹ️ **LR 소진 주의** → 3M 이후에도 학습이 필요하다면 warm restart 또는 새 LR 스케줄로 재시작 권장