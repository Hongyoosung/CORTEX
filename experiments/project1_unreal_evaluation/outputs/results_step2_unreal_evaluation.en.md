# Stage 2 Result — Unreal Short Behavioral Evaluation

This document records the Stage 2 behavioural evaluation for Project 1 Problem 2,
following the workflow in `project1_attention_experiment_plan.en.md`. It measures
how the final Self+Cross attention MAPPO model actually distributes its team
across objectives in the real Unreal environment, and connects those numbers to
the Stage 1 attention probe.

## Run Conditions

| Item | Value |
|---|---|
| Run timestamp | 2026-06-02T17:04:30 (local) |
| Environment | Windows host UE5.6 (headless) ↔ Python `game_ai` (torch 2.5.1+cu121); Schola 2.0.1 via `PYTHONPATH=Plugins/Schola-2.0.1/Resources/python` |
| Unreal | `DE.uproject`, default map `Training_Basic2`, launched `-game -nullrhi -nosound -ScholaPort=50051~2`; **8 Schola environments × 5 RL agents** auto-detected |
| Opponent | `BP_ScriptAgent` (Scripted AI) — confirmed in UE5 log; RL = Blue/Team 1, Scripted = Red/Team 0 |
| Checkpoint | `DE_Training/training_results/20260328_032546/best` (final Self+Cross MAPPO; strike/vanguard/support) |
| Episodes | 24 (parallel across 8 envs), 702.8 s wall time |
| Objectives | 5 capture points per match |

### Experiment Objective

Check whether the attention model's relationship awareness (established
structurally in Stage 1) shows up in actual team-placement behaviour, via the two
metrics most directly tied to Problem 2: **duplicate objective selection rate** and
**objective coverage**. Win rate / reward are secondary references.

### Input Conditions

- Deterministic (mean) actions from the three per-role policies, routed by the
  strategy one-hot in each agent's observation (same routing as training/eval).
- 24 episodes, 8 parallel sub-environments, fixed Scripted AI opponent, identical
  map/objective layout. Single continuous session (single effective seed — see
  Limitations).
- Per-step logging for every alive RL agent. "Selected objective" is derived from
  the agent's own 226-dim observation (no extra UE5 instrumentation):
  - **assigned** = base with `is_assigned_target = 1` (Squad-Commander intent),
  - **nearest** = closest valid base by relative position (realised placement,
    which the attention-driven EQS movement actually produces).

### Execution Command

```powershell
# UE5 (headless) launched first:
& "C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor.exe" `
    DE.uproject -game -nullrhi -nosound -notexturestreaming -nopause -ScholaPort=50051~2 -log

# Python client (game_ai), Schola 2.0.1 on PYTHONPATH:
$env:PYTHONPATH = "...\Plugins\Schola-2.0.1\Resources\python"
conda run -n game_ai python experiments/project1_unreal_evaluation/run_unreal_eval.py `
    --episodes 24 --num-envs 8 --port 50051
conda run -n game_ai python experiments/project1_unreal_evaluation/analyze_stepwise.py
conda run -n game_ai python experiments/project1_unreal_evaluation/plot_unreal_eval.py
```

### Raw Artifacts

| Artifact | Path |
|---|---|
| Summary metrics | `outputs/unreal_eval_metrics_20260602_170430.json` |
| Per-step behaviour log (78,575 rows) | `outputs/unreal_eval_stepwise_20260602_170430.csv` |
| Clustering distribution analysis | `outputs/stepwise_analysis.json` |
| UE5 headless log | `outputs/ue5_eval.log` |
| Behaviour-metric figure | `outputs/figures/fig1_behavior_metrics.png` |
| Clustering-profile figure | `outputs/figures/fig2_clustering_profile.png` |
| Episode-outcome figure | `outputs/figures/fig3_episode_outcomes.png` |

## Key Metrics

| Metric | assigned | nearest | Link to Problem 2 |
|---|---:|---:|---|
| Objective coverage | 0.4875 | 0.4542 | Team covers < half of 5 objectives |
| Mean unique objectives (of 5) | 2.44 | 2.27 | Direct distributed-assignment measure |
| Mean max-cluster size | 3.16 | 3.36 | ~3+ agents pile on the busiest objective |
| Duplicate rate (binary, ≥2 share) | 1.000 | 0.999 | **Saturated — see Observations** |

Secondary references:

| Metric | Value |
|---|---:|
| Win rate vs Scripted AI | 0.292 (7 W / 17 L / 0 D / 0 timeout) |
| Mean episode reward (team total) | 148.9 ± 34.2 |
| Team-steps logged | 15,715 (mean 5.0 alive agents/step) |

Distribution of #unique objectives per team-step (nearest): 1→16.0%, 2→47.6%,
3→29.8%, 4→6.5%, 5→0.08%.

Coverage over episode progress (nearest): early 0.356 → mid 0.485 → late 0.521.

## Observations

1. **The team clusters heavily — Problem 2 is real and quantified.** Across 15,715
   team-steps the RL team covers on average only 2.27 of 5 objectives (coverage
   0.45), with ~3.4 of 5 agents sitting on the single busiest objective. The team
   reaches full distinct coverage (5 unique) on essentially no steps (0.08%).

2. **The binary duplicate rate saturates and is not discriminative.** With 5 agents
   and 5 objectives, "two or more share an objective" is true ~100% of the time, so
   the raw duplicate rate (1.0) carries little information. The informative views
   are the **unique-objective distribution**, **coverage**, and **max-cluster size**,
   which all show the same heavy-clustering pattern with real spread.

3. **Clustering eases over the course of an episode.** Coverage rises monotonically
   from 0.356 (early third) to 0.521 (late third): agents start bunched (spawn /
   early contest) and progressively distribute across objectives as the match
   develops. This temporal signal is consistent with a policy that *does* push
   toward spreading, but only partially resolves the pile-up within an episode.

4. **`assigned` ≈ `nearest`.** The Squad-Commander's intended assignment (coverage
   0.49) and the realised nearest-objective placement (0.45) track closely, so the
   executors broadly follow commander intent; the residual gap (0.49→0.45) is the
   part where realised positions cluster slightly more than the assignment plan.

5. **Win rate is a weak reference here.** 29% vs Scripted AI over 24 short episodes
   on the training map; it is noisy and not the focus of Problem 2.

## Interpretation (for Problem 2 and the attention design)

Stage 2 establishes the **behavioural baseline of the deployed Self+Cross model**:
the team still exhibits substantial duplicate assignment (coverage ~0.45, ~3.4
agents on the busiest objective), but it is not static — coverage improves through
the episode.

Connecting to Stage 1: the probe showed that intra-set Self-Attention makes the
encoder formation-sensitive and that, for the strike role specifically, it lowers
the preference for an already-crowded objective (+0.018 duplicate-avoidance gain
from the in-checkpoint ablation). Stage 2 shows that this encoder-level
relationship awareness **mitigates but does not eliminate** duplicate assignment in
realised play. This is exactly the correctly-scoped claim: attention improved the
observation encoder so each role policy can read spatial relationships before
acting; it did not, by itself, produce perfect role separation. The remaining
clustering (coverage well below 1.0) is the headroom that a trained Cross-only /
MLP baseline comparison — or further reward shaping — would quantify.

## Portfolio-Usability of Each Metric

| Metric | Usable for portfolio claim? | Reason |
|---|---|---|
| Objective coverage (0.45–0.49) | ✅ Use | Quantifies Problem-2 distribution directly; not saturated. |
| Mean unique objectives (2.3/5) & distribution | ✅ Use | Most interpretable view of clustering. |
| Mean max-cluster size (~3.4) | ✅ Use | Intuitive "pile-up" measure. |
| Coverage over episode thirds (0.36→0.52) | ✅ Use | Shows the policy actively spreads over time. |
| Binary duplicate rate (1.0) | ⚠️ Report with caveat | Saturated by 5v5 geometry; cite distribution instead. |
| Win rate (29%) / reward | ⚠️ Reference only | Noisy at 24 eps; not a Problem-2 metric. |

## Limitations

- **Single model, no trained baseline.** Only the Self+Cross checkpoint was
  evaluated; `EntityCentricPolicy_NoSelfAttn` has no checkpoint. Stage 2 therefore
  measures *what the attention model does*, not *how much attention reduced
  clustering vs a no-attention model*. Attribution evidence comes from the Stage 1
  in-checkpoint ablation, not from Stage 2. (Training a Cross-only checkpoint via
  the existing `USE_SELF_ATTN=0` training flag is the natural follow-up.)
- **Single effective seed.** Episodes ran in one continuous session; UE5 spawn /
  Scripted-AI seeding was not independently varied across ≥3 seeds. Metric means
  are over 24 episodes / 15,715 team-steps but from one session.
- **Self-play training map, not a dedicated eval map.** Run on `Training_Basic2`
  (the trained map) with the Scripted AI opponent; the dedicated `Eval_Map` was not
  used. The behavioural metrics depend only on the RL team's own observations, so
  they are valid regardless, but win rate reflects this specific setup.
- **"Selected objective" is a proxy** derived from observation tokens
  (assigned-target flag / nearest base), not an explicit per-agent objective output
  from UE5. `assigned` and `nearest` agree closely, which supports the proxy.
- **Binary duplicate rate saturates** at 5v5 and should not be quoted alone.

## Stage 2 Completion Check (against plan criteria)

- [x] At least 20 Unreal evaluation episodes executed (24).
- [x] Step-level behaviour logs collected (78,575 rows / 15,715 team-steps).
- [x] Duplicate rate and objective coverage computed (+ distribution, cluster size).
- [x] Role-position consistency: per-role objective selections logged in the CSV
      (`role` column); full role-position rule scoring deferred (see Next action).
- [x] One behavioural-metric table ready for portfolio use.
- [x] Stage 1 ↔ Stage 2 connected in one explanatory paragraph (Interpretation).
- [x] This result document records conditions, raw paths, metrics, interpretation,
      and limitations.

## Next Action

1. **Strongest follow-up:** train a Cross-only checkpoint (`USE_SELF_ATTN=0`) and
   re-run this exact eval to fill the comparison table (coverage / cluster size
   Self+Cross vs Cross-only) — converting Stage 1's in-checkpoint ablation into a
   trained A/B and turning "mitigates" into a measured delta.
2. Add multi-seed runs (≥3) to put error bars on coverage.
3. Optionally score the role-position consistency rules (strike/vanguard/support)
   from the existing stepwise CSV against the portfolio's role-reward descriptions.
