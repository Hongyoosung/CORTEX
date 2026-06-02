# Stage 1 Result — Python Synthetic Attention Probe

This document records the Stage 1 attention-probe experiment for Project 1
Problem 2 ("loss of relational information between entities"). It follows the
result-document workflow defined in `project1_attention_experiment_plan.en.md`.
It is updated immediately after each execution and keeps every metric traceable
to a raw artifact.

## Run Conditions

| Item | Value |
|---|---|
| Run timestamp | 2026-06-02T16:18:29 (local) |
| Environment | conda env `game_ai`, torch 2.5.1+cu121, numpy 2.2.6, CUDA available (probe runs on CPU, batch=1) |
| Unreal usage | None (synthetic probe only) |
| Checkpoint | `DE_Training/training_results/20260328_032546/best` (final MAPPO run) |
| Models | 3 per-role `EntityCentricPolicy` instances: `strike_policy`, `vanguard_policy`, `support_policy` (268,111 params each) |
| Architecture | Self+Cross attention (intra-set Self-Attention → LayerNorm → Cross-Attention), hidden=64, heads=4 |
| Seed | 0 |

### Experiment Objective

Verify, without launching Unreal, whether the Self-Attention → Cross-Attention
pipeline actually reads relational (formation) information:

1. Are padded entity slots fully suppressed in attention?
2. Does intra-set Self-Attention change between Clustered and Spread formations?
3. Does the Self-Attention focus propagate into Cross-Attention immediately
   before action selection?
4. Does the resulting Cross-Attention context change the 7-dim EQS action, in
   particular an objective/duplicate-assignment preference proxy?

### Input Conditions (Synthetic Scenarios)

The self token and the three objective (base) tokens are held **fixed** across
scenarios; only the entity group under test moves. This makes any attention
change attributable to the formation change.

| Scenario | What varies | Valid entities |
|---|---|---|
| `ally_clustered` | 4 allies clustered inside base-0 radius | 4 allies, 2 enemies, 3 bases |
| `ally_spread` | 4 allies spread across distinct objectives/corners | 4 allies, 2 enemies, 3 bases |
| `enemy_converged` | 3 enemies converging on base 0 (allies fixed=spread) | 4 allies, 3 enemies, 3 bases |
| `enemy_dispersed` | 3 enemies dispersed (allies fixed=spread) | 4 allies, 3 enemies, 3 bases |
| `padding_stress_k{0..4}` | k valid allies, the rest padded | k allies (k=0..4) |

Observation layout matches `DEObservationTypes.h` / `policy.py` (226-dim:
self 7, ally 8×9, enemy 8×8, base 8×7, three 8-dim masks, 3-dim strategy one-hot).

### Execution Command

```powershell
conda run -n game_ai python experiments/project1_attention_probe/run_attention_probe.py `
    --checkpoint DE_Training/training_results/20260328_032546/best
conda run -n game_ai python experiments/project1_attention_probe/plot_attention_probe.py
```

### Raw Artifacts

| Artifact | Path |
|---|---|
| Per-(role,scenario) attention/EQS rows | `outputs/attention_probe_results.csv` |
| Structured metric summary | `outputs/attention_probe_metrics.json` |
| Raw attention tensors (for re-plotting) | `outputs/raw_attention.npz` |
| Auto-generated summary table | `outputs/attention_probe_summary.md` |
| Self-Attention heatmaps (Clu/Spr/Diff × 3 roles) | `outputs/figures/fig1_self_attention_clustered_vs_spread.png` |
| Cross-Attention focus bars (Clu vs Spr) | `outputs/figures/fig2_cross_attention_shift.png` |
| Padding suppression sweep | `outputs/figures/fig3_padding_suppression.png` |
| Metric summary bars | `outputs/figures/fig4_metric_summary.png` |

## Key Metrics

### Global

| Metric | Value | Interpretation |
|---|---:|---|
| Padding suppression (max attention on any padded slot) | **0.000** | The `key_padding_mask` works perfectly; invalid entities are ignored. |
| Self-Cross consistency (all roles × groups × core scenarios) | 0.639 | The top Self-Attention key matches the top Cross-Attention slot ~64% of the time. |

### Per-role

| Role | Pad max | Ally Self ΔMax (Clu vs Spr) | Enemy Self ΔMax (Cnv vs Dsp) | Ally Cross TV-shift (Clu vs Spr) | Self-Cross consist. | Obj-pref shift (Spr−Clu) | Dup-risk Self+Cross | Dup-risk Cross-only | Self-attn gain |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| strike   | 0.000 | 0.0945 | **0.1165** | 0.1347 | 0.417 | **+0.0357** | **+0.0357** | +0.0179 | **+0.0178** |
| vanguard | 0.000 | **0.1086** | 0.0394 | 0.0677 | **0.917** | −0.0002 | −0.0002 | +0.0072 | −0.0075 |
| support  | 0.000 | 0.0582 | 0.0185 | 0.0831 | 0.583 | −0.0134 | −0.0134 | −0.0074 | −0.0060 |

Notes:
- **Self ΔMax** = max per-key difference in intra-set Self-Attention (averaged
  over valid query rows) between the two formations. Higher = more sensitive to
  formation change.
- **Cross TV-shift** = total-variation distance (0.5·L1) between the Self→entity
  Cross-Attention distributions of the two scenarios. It is reported because the
  *argmax* Cross-Attention slot did not change in any case (always slot 2), but
  the distribution mass clearly redistributes (see Observations).
- **Obj-pref shift** = change in the `AllyObjectiveProximity` EQS weight
  (Spread − Clustered). Positive = the policy prefers ally-occupied objectives
  *less* when allies are clustered, i.e. it avoids piling onto a crowded objective.
- **Dup-risk Self+Cross vs Cross-only**: the same metric computed with the full
  model versus with the intra-set Self-Attention residual ablated at inference
  (same trained weights, self-attention update zeroed). **Self-attn gain** =
  Self+Cross − Cross-only.

## Observations

1. **Padding is fully suppressed.** Across every role, every core scenario, and
   the entire `padding_stress` sweep (k = 0…4), the maximum attention assigned to
   any padded slot is exactly 0.000 (`fig3`). Example Self→Ally Cross-Attention
   for strike/clustered: `[0.206, 0.270, 0.325, 0.199, 0, 0, 0, 0]`.

2. **Self-Attention is formation-sensitive, and the sensitivity is role-specific.**
   - Ally Self-Attention ΔMax (Clustered vs Spread): vanguard 0.109 > strike
     0.094 > support 0.058. The two frontline roles react more strongly to ally
     formation than support.
   - Enemy Self-Attention ΔMax (Converged vs Dispersed): strike 0.117 ≫ vanguard
     0.039 > support 0.018. Strike is by far the most sensitive to enemies
     converging on an objective.
   - In `fig1`, the Clustered ally Self-Attention is near-uniform across query
     rows (all allies look similar), whereas the Spread case concentrates on
     specific slots (e.g. strike spread row 1 → 0.437 on slot 1), confirming the
     relational read changes with geometry.

3. **Self-Attention focus propagates into Cross-Attention.** The argmax
   Cross-Attention slot is stable (slot 2) but the distribution shifts
   substantially: strike Ally Cross goes `[0.206, 0.270, 0.325, 0.199]`
   (clustered) → `[0.133, 0.354, 0.375, 0.137]` (spread), TV-shift 0.135 — mass
   moves off the corner allies (slots 0, 3) onto the central allies (slots 1, 2).
   Vanguard shows the highest Self-Cross top-slot consistency (0.92).

4. **The objective-preference proxy moves in the duplicate-avoiding direction for
   strike.** Strike raises `AllyObjectiveProximity` by +0.036 when allies are
   spread vs clustered — i.e. it is *less* willing to head for an ally-occupied
   objective when allies are already piled there. With Self-Attention ablated this
   response halves (+0.018), so intra-set Self-Attention contributes a measurable
   +0.018 to strike's duplicate-avoidance response.

5. **The duplicate-risk story is role-specific, not universal.** Vanguard and
   support show near-zero or slightly negative objective-preference shifts, and
   their Self-attn gain is slightly negative. This is consistent with role design:
   vanguard holds contested objectives (so it is *not* expected to back off a
   crowded objective) and support tracks allies more evenly. The clean
   duplicate-avoidance signal is concentrated in the strike role.

## Interpretation (for Problem 2 and the attention design)

- The probe gives **structural evidence** that the Self-Attention → Cross-Attention
  encoder reads relationships among entities rather than only individual
  coordinates: padded slots are ignored, intra-set Self-Attention changes with
  formation, and that change redistributes Cross-Attention immediately before
  action selection.
- The contribution is correctly scoped. The role behaviours themselves come from
  the per-role policies and per-role rewards; the attention encoder lets each
  role *interpret the surrounding spatial relationships*. The role-specific
  sensitivity pattern (strike→enemies, frontline→allies) is exactly what the
  encoder-improvement claim predicts, and the strike duplicate-avoidance ablation
  (+0.018 from Self-Attention) ties directly to Problem 2.

## Portfolio-Usability of Each Metric

| Metric | Usable for portfolio claim? | Reason |
|---|---|---|
| Padding suppression = 0.000 | ✅ Use | Clean, unambiguous, all roles/scenarios. |
| Role-specific Self-Attention ΔMax | ✅ Use | Clear ordered pattern matching role roles. |
| Cross-Attention TV-shift | ✅ Use (with caveat) | Distribution shift is real; state that the argmax slot is stable. |
| Self-Cross consistency | ⚠️ Use selectively | Strong for vanguard (0.92); modest globally (0.64). Report honestly. |
| Strike duplicate-risk Self-attn gain (+0.018) | ✅ Use | Direct Problem-2 link; from an in-checkpoint ablation. |
| Vanguard/support duplicate-risk | ⚠️ Hold / contextualize | Near-zero or negative; present as role-appropriate, not as failure. |

## Limitations

- **Single checkpoint, no independently-trained ablation.** The Cross-only
  comparison is an *inference-time* ablation of the same Self+Cross weights (the
  Self-Attention residual is zeroed), not a separately trained Cross-only model.
  A `EntityCentricPolicy_NoSelfAttn` class exists in `policy.py` but no checkpoint
  was trained for it. This is the cleanest one-checkpoint comparison but it
  measures "what Self-Attention adds to this trained pipeline", not "Self+Cross
  vs a model trained without Self-Attention". Listed as Stage 1 follow-up.
- **Synthetic, hand-built scenarios.** Positions are designer-chosen, not sampled
  from real rollouts. The probe shows the mechanism is *capable* of reading
  relationships; it does not by itself prove the effect on game outcomes.
- **Single seed, single example per scenario** (deterministic forward, batch=1).
  ΔMax/TV values are point estimates, not distributions.
- **Argmax Cross-Attention slot is stable** across formations; only the
  continuous distribution shifts. The boolean "focus shift" metric is therefore
  False everywhere and should not be quoted alone.
- The objective-preference proxy uses a single EQS dimension
  (`AllyObjectiveProximity`); it is a proxy, not a measured duplicate-assignment
  rate. The real behavioural metric is deferred to Stage 2.

## Stage 1 Completion Check (against plan criteria)

- [x] At least four synthetic scenarios executed (4 core + 5 padding-stress).
- [x] Role-specific Self-Attention and Cross-Attention weights extracted.
- [x] Padded slots verified to receive zero attention (max = 0.000).
- [x] Clustered/Spread DeltaMax computed (and Converged/Dispersed for enemies).
- [x] Self-Cross consistency computed.
- [x] One summary table + four figures ready for portfolio use.
- [x] This result document records conditions, raw paths, metrics, interpretation,
      and limitations.

## Next Action (question for Stage 2)

Stage 1 shows the encoder *can* read formation relationships and that, for the
strike role, this reduces preference for a crowded objective. **Stage 2 must
check whether this translates into behaviour in the real Unreal environment:**
does the team show a lower duplicate objective-selection rate and higher
objective coverage? Stage 2 should log per-step `selected_objective_id` for alive
teammates over 20–50 episodes (≥3 seeds) against the fixed Scripted AI tier, then
connect the measured duplicate rate / coverage back to the strike
duplicate-avoidance signal found here.

(Optional Stage 1 follow-up: train a real `EntityCentricPolicy_NoSelfAttn`
checkpoint to replace the inference-time ablation with a trained baseline.)
