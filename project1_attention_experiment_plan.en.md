# Project 1 Attention Validation Experiment Plan

## Purpose

Problem 2 in Project 1 addresses "loss of relational information between entities" by inserting Intra-Set Self-Attention before Cross-Attention. The purpose of this plan is to progressively collect quantitative and qualitative evidence that can be used in the portfolio.

The core questions are:

1. Does attention actually recognize relationships between entities?
2. Is that relationship awareness reflected in the information selection immediately before action?
3. Does it lead to behavioral improvements such as lower duplicate assignment, higher objective coverage, or more stable rewards/win rate?

The experiment proceeds in two stages.

| Stage | Purpose | Environment | Deliverables |
|---|---|---|---|
| Stage 1 | Verify whether the attention structure reads relational information | Python synthetic probe | Attention weights, action proxy, quantitative table, Stage 1 result document |
| Stage 2 | Verify behavioral metrics in the real game environment | Short Unreal evaluation | Duplicate assignment rate, objective coverage, short win-rate/reward logs, Stage 2 result document |

Unreal project path:

```text
C:\Users\PC\Documents\GitHub\DE\DE.uproject
```

## Experiment Principles

Do not overstate the contribution of attention. The current project already uses role-specific policy networks and role-specific reward functions, so the claim should not be "attention created the roles."

The accurate claim is:

> The roles themselves were learned through role-specific policies and reward design. Attention improved the observation encoder so that each role policy could read spatial relationships among allies, enemies, and objectives before selecting an action.

Therefore, Stage 1 produces structural evidence for relationship awareness, and Stage 2 checks whether that relationship awareness affected actual behavioral metrics.

## Result Document Workflow

Create stage-specific result documents separately from this plan. Do not wait until all experiments are finished to write them. Update the relevant result document immediately after each step. This keeps the source of each metric and interpretation traceable, and it also records failed experiments so they can be explained instead of silently discarded.

Recommended result document paths:

```text
experiments/
  project1_attention_probe/
    results_step1_attention_probe.en.md
  project1_unreal_evaluation/
    results_step2_unreal_evaluation.en.md
```

Each result document should contain at least:

| Item | Content |
|---|---|
| Run timestamp | Date and time of the experiment |
| Environment | Python/torch version, Unreal usage, checkpoint path |
| Experiment objective | The question being tested in the step |
| Input conditions | Scenario, seed, episode count, map, opponent tier |
| Execution command | Reproducible command or procedure |
| Raw artifacts | CSV/JSON/log/figure paths |
| Key metrics | Duplicate rate, coverage, DeltaMax, etc. |
| Observations | Patterns visible in the numbers |
| Interpretation | Meaning for Problem 2 and attention design |
| Limitations | Missing baseline, low episode count, low seed count, etc. |
| Next action | Follow-up work for the next step |

After every step:

1. Add the run conditions and raw output paths to the result document.
2. Summarize the key metrics in a table.
3. Mark whether each metric is usable for a portfolio claim or should be held back.
4. Keep failed or abnormal results with an `Excluded/Invalid` reason.
5. Update the question to verify in the next step.

## Stage 1: Python Synthetic Attention Probe

### Goal

Verify whether the Self-Attention -> Cross-Attention pipeline behaves as intended using controlled synthetic inputs without launching Unreal.

This stage does not claim real win-rate improvement. It checks whether the following are observable inside the model:

- Padded slots are fully suppressed in attention.
- Self-Attention distributions change between Clustered and Spread formations.
- Focus changes produced by Self-Attention propagate into Cross-Attention.
- Cross-Attention results affect the 7-dimensional EQS action or an objective-preference proxy.

### Comparison Targets

If possible, compare the following three structures.

| Model | Description | Purpose |
|---|---|---|
| MLP baseline | Flatten entity vectors and process them with an MLP | Attention-free baseline |
| Cross-Attention only | Self Token attends to raw entity tokens | Check individual entity selection |
| Self + Cross Attention | Intra-set Self-Attention before Cross-Attention | Check relationship-aware encoding |

If only the Self + Cross Attention checkpoint exists, first run the probe on the current checkpoint. Extend the same script later when ablation checkpoints are prepared.

### Synthetic Scenarios

Create at least four scenarios.

| Scenario | Description | Purpose |
|---|---|---|
| Ally Clustered | Four allies clustered within the same objective radius | Detect duplicate-assignment context |
| Ally Spread | Four allies spread across corners/objectives | Detect distributed formation |
| Enemy Converged | Two or more enemies moving toward the same objective | Detect converging threat pattern |
| Padding Stress | Vary valid entities from 0 to 4 and pad the rest | Verify mask stability |

Keep the same self state and objective state while changing only entity positions. This makes it easier to explain that attention changes are caused by formation changes.

### Metrics

| Metric | Calculation | Interpretation |
|---|---|---|
| Padding suppression | Mean/max attention assigned to padded slots | Near zero means the mask works |
| Self-Attention DeltaMax | Maximum slot-weight difference between Clustered and Spread | Sensitivity to formation changes |
| Cross-Attention focus shift | Whether the top Cross-Attention slot changes | Change in pre-action reference target |
| Self-Cross consistency | Match rate between top Self-Attention and top Cross-Attention slots | Pipeline consistency |
| Objective preference shift | Change in per-objective action/EQS proxy | Whether relationship awareness affects action preference |
| Duplicate-risk proxy | Preference decrease for an already crowded objective | Directly tied to Problem 2 |

### Expected Portfolio Table

Prepare results in a format like this.

| Validation Item | Example Result | Portfolio Interpretation |
|---|---:|---|
| Padding suppression | padded slot max = 0.000 | Invalid entities are ignored reliably |
| Vanguard Self-Attention DeltaMax | 0.29 | Frontline role is sensitive to formation changes |
| Support Self-Attention DeltaMax | 0.06 | Support monitors allies more evenly |
| Self-Cross consistency | 90%+ | Relational representation reaches pre-action selection |
| Duplicate-risk proxy decrease | Self+Cross > Cross-only | Directly connected to duplicate assignment |

### Implementation Plan

1. Inspect the current policy/model definition and checkpoint loading path.
2. Document the observation vector layout.
3. Implement a synthetic observation builder.
4. Extract attention weights through forward hooks or explicit model returns.
5. Save scenario-wise attention/action results to CSV/JSON.
6. Generate heatmaps and bar charts with matplotlib/seaborn.
7. Update the result document immediately after each execution step.

Recommended file structure:

```text
experiments/
  project1_attention_probe/
    build_synthetic_obs.py
    run_attention_probe.py
    metrics.py
    plot_attention_probe.py
    outputs/
      attention_probe_results.csv
      attention_probe_summary.md
      results_step1_attention_probe.en.md
      figures/
```

### Stage 1 Completion Criteria

Stage 1 is complete when:

- At least four synthetic scenarios have been executed.
- Role-specific Self-Attention and Cross-Attention weights have been extracted.
- Padded slots are verified to receive zero or near-zero attention.
- Clustered/Spread DeltaMax has been computed.
- Self-Cross consistency has been computed.
- One table and two to three figures are ready for portfolio use.
- The Stage 1 result document includes run conditions, raw artifact paths, metrics, interpretation, and limitations.

## Stage 2: Unreal Short Evaluation

### Goal

Run a short evaluation in the actual Unreal environment to check whether the attention structure affects behavioral metrics.

This stage should focus on evaluation rather than long retraining. The most important metrics are duplicate assignment rate and objective coverage, because they directly correspond to Problem 2.

### Execution Target

Unreal project:

```text
C:\Users\PC\Documents\GitHub\DE\DE.uproject
```

Prepare evaluation targets in this order if possible.

| Priority | Model | Reason |
|---|---|---|
| 1 | Current best Self + Cross Attention checkpoint | Main portfolio model |
| 2 | Cross-Attention only checkpoint | Compare Self-Attention contribution |
| 3 | MLP/flatten baseline checkpoint | Compare total attention contribution |

If no baseline checkpoint exists, first measure behavioral metrics for the current model. Expand the table later when baselines are available.

### Metrics

| Metric | Definition | Link to Problem 2 |
|---|---|---|
| Duplicate objective selection rate | Ratio of steps where two or more teammates select the same objective | Direct duplicate assignment metric |
| Objective coverage | Number of unique objectives covered by the team per step | Measures distributed assignment |
| Over-cluster duration | Duration where too many allies stay inside the same objective radius | Measures clustered-state resolution |
| Role-position consistency | Ratio of steps satisfying expected role-specific positioning | Validates role-specific spatial reasoning |
| Win rate | Episode win ratio against scripted AI | Final performance reference |
| Mean episode reward | Mean/variance of episode reward | Training stability reference |

Priority order:

1. Duplicate objective selection rate
2. Objective coverage
3. Role-position consistency
4. Win rate
5. Mean episode reward

Win rate is powerful but can be noisy in short evaluations. Duplicate rate and coverage are more directly tied to the cause and solution of Problem 2.

### Recommended Evaluation Settings

| Item | Recommended Value | Reason |
|---|---:|---|
| Episodes | 20-50 | Balance between runtime and statistical stability |
| Random seeds | 3 or more | Reduce single-seed bias |
| Opponent | Fixed Scripted AI tier | Keep comparison conditions stable |
| Map/objectives | Same map and same objective layout | Minimize unrelated variables |
| Logging interval | Per step | Required for duplicate rate and coverage |

### Unreal Logging Plan

Record at least the following at each step.

```text
episode_id
step
team_id
agent_id
role
position
selected_objective_id
objective_distances
objective_owner_states
eqs_action_7d
alive
reward
done
```

If possible, also record attention weights.

```text
ally_self_attention
ally_cross_attention
enemy_cross_attention
base_cross_attention
```

If attention-weight logging is difficult in Unreal, record behavior metrics only in Stage 2 and connect them to the Stage 1 Python attention analysis.

### Analysis

Duplicate objective selection rate:

```text
duplicate_rate = duplicate_steps / valid_team_steps
```

At each step, collect the `selected_objective_id` for alive teammates. If two or more agents select the same objective, count it as a duplicate step.

Objective coverage:

```text
coverage = unique(selected_objective_id) / min(num_alive_agents, num_objectives)
```

Role-position consistency examples:

| Role | Example Condition |
|---|---|
| Strike | Stays away from enemies while approaching non-friendly objectives |
| Vanguard | Holds contested/neutral objectives and maintains frontline distance |
| Support | Stays within support distance of allies and keeps rear/mid positioning |

The role conditions should match the existing role reward descriptions in the portfolio.

### Expected Result Tables

If comparison checkpoints are available:

| Model | Duplicate rate down | Objective coverage up | Role consistency up | Win rate up |
|---|---:|---:|---:|---:|
| Cross-Attention only | TBD | TBD | TBD | TBD |
| Self + Cross Attention | TBD | TBD | TBD | TBD |

If only the current model is available:

| Metric | Value | Interpretation |
|---|---:|---|
| Duplicate rate | TBD | Frequency of duplicate objective selection |
| Objective coverage | TBD | Average objective coverage by the team |
| Role consistency | TBD | Role-specific positioning consistency |
| Win rate | TBD | Reference performance against scripted AI |

### Stage 2 Completion Criteria

Stage 2 is complete when:

- At least 20 Unreal evaluation episodes have been executed.
- Step-level behavior logs have been collected.
- Duplicate rate and objective coverage have been computed.
- Role consistency has been computed if feasible.
- One behavioral metric table is ready for portfolio use.
- Stage 1 attention-probe results and Stage 2 behavior metrics are connected in one explanatory paragraph.
- The Stage 2 result document includes run conditions, raw artifact paths, metrics, interpretation, and limitations.

## Portfolio Wording Draft

After the experiments, summarize the results in this direction:

> Self-Attention allows ally, enemy, and objective tokens to reference other tokens in the same set, enabling the model to represent spatial relationships such as clustering, spreading, and duplicate assignment rather than only individual entity coordinates. In the synthetic probe, role-specific attention sensitivity changed under Clustered and Spread formations, and the focus produced by Self-Attention propagated into Cross-Attention immediately before action selection. In the Unreal evaluation, duplicate objective selection rate and objective coverage were measured to verify how relationship awareness affected actual team-placement behavior.

Avoid this:

```text
Bad: Attention alone caused roles to emerge spontaneously.
Good: Attention-based observation encoding allowed role-specific policies to interpret surrounding spatial relationships differently.
```

## Execution Summary

1. Implement the Python synthetic observation builder.
2. Extract attention weights from the current checkpoint.
3. Produce Clustered/Spread/Padding scenario results.
4. Generate attention-probe tables and figures.
5. Check Unreal evaluation log fields.
6. Run a short 20-50 episode Unreal evaluation.
7. Compute duplicate rate and objective coverage.
8. Write the document that will be reflected in the portfolio Problem 2 and Attention Validation sections.

Update the relevant result document immediately after each numbered step. Steps 2, 3, 6, and 7 are especially important because raw outputs and interpretation can easily become separated if they are not recorded right away.

## Risks and Responses

| Risk | Response |
|---|---|
| No baseline checkpoint exists | Complete the current-model probe first and list ablation as follow-up |
| Unreal execution/build cost is high | Add Stage 1 Python results to the portfolio first, then measure only one or two Stage 2 behavior metrics |
| Win rate variance is high | Use duplicate rate and objective coverage as primary metrics |
| Attention-action link is weak | Present Self-Cross consistency and objective preference shift together |
| "Role differentiation" sounds overstated | Use "role-specific interpretation of spatial relationships" instead |

