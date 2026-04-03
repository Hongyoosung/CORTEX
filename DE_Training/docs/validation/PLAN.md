# Validation Project — Overall Plan

## Goal

Validate the research claims of the paper through a phased experimental pipeline. Phase 1 (PoC) uses a lightweight MiniGrid environment to establish that the core mechanism exists. Phase 2 (Main) ports to SMACv2 for benchmark reproducibility. Phase 3 produces the paper.

---

## Phase 1: PoC (MiniGrid)

**Objective:** Confirm that the three testable predictions from the theory hold in a controlled, fast-iteration environment.

**Entry point:** See `AGENT_BRIEF.md` for full implementation specification.

### Deliverables

| # | Deliverable | Success Criterion |
| :--- | :--- | :--- |
| 1 | Custom 3-role MiniGrid environment | 5 ablation conditions train stably without NaN or reward collapse |
| 2 | Track 1: Win rate curves for all 5 conditions | Order: `Full > No_SelfAttn > Role+MLP > No_RoleLabel > Flat_MLP` |
| 3 | Track 2: Self-attention heatmaps (Clustered vs. Spread) | Role-specific Δmax patterns: Vanguard highest, Support lowest |
| 4 | Track 2: 3×3 role-routing matrix | Strike→Support weight is notably higher than Strike→Vanguard |
| 5 | Track 2: MLP gradient attribution | Role_onehot gradient contribution is lower/less structured than attention weights |
| 6 | Claim confirmation checklist | ≥4 of 5 checks pass |

### Go/No-Go Decision After Phase 1

| Result | Decision |
| :--- | :--- |
| ≥4/5 checks pass | Proceed to Phase 2 |
| 3/5 pass, Track 2 patterns visible | Proceed to Phase 2, narrow claim scope |
| Role-routing matrix is diagonal-only (same-role attention) | Interesting but different result — reframe, still publishable |
| Full ≈ Role+MLP AND no attention structure | Stop. Claim does not hold in this environment. Revisit observation design. |

---

## Phase 2: Main Experiments (SMACv2)

**Objective:** Reproduce the mechanism on a standard MARL benchmark to establish reproducibility and reviewer credibility.

### Environment Mapping

SMACv2 does not have explicit Strike/Vanguard/Support roles. Use **protoss unit types** as role proxies:

| Paper Role | SMACv2 Unit | Justification |
| :--- | :--- | :--- |
| Strike | Stalker | Ranged DPS, positional play |
| Vanguard | Zealot | Melee, frontline |
| Support | Colossus | Area splash, positioning-dependent |

Map: `protoss_5_vs_5` or similar heterogeneous-unit scenario.

**Role label:** Unit type one-hot, injected into `self_tok` and into each ally token. This preserves the Q/K/V conditioning mechanism without modifying SMACv2 internals.

### Additional Baselines for Phase 2

Beyond the 5 ablation conditions from Phase 1, add:

| Baseline | Why |
| :--- | :--- |
| QMIX (vanilla) | Standard cooperative baseline, no attention |
| MAPPO vanilla (flat obs) | Shows benefit over naive MAPPO |
| ROMA | Closest prior work — automatic role discovery vs. label-conditioned |

### Deliverables

| # | Deliverable |
| :--- | :--- |
| 1 | 5-condition ablation on SMACv2 protoss map (Track 1) |
| 2 | 3 additional baselines (QMIX, MAPPO vanilla, ROMA) |
| 3 | Track 2 analysis replicated on SMACv2 checkpoints |
| 4 | Win rate tables and learning curves suitable for paper figures |

---

## Phase 3: Paper Writing

### Section Writing Order (Recommended)

Write in this order — start from what the data proves, then build the framing around it.

1. **Experiments section** — write directly from Track 1 and Track 2 outputs. This is the factual core.
2. **Method section** — describe the architecture and 5 conditions exactly as implemented.
3. **Theory section** — write the Q/K/V derivation and role-pair routing argument. Depth depends on Track 2 results:
   - If role-routing matrix shows clear cross-role patterns → full formal treatment
   - If patterns are present but weak → keep theory at "inductive bias" level, let visualization carry the argument
4. **Introduction** — frame the reward-engineering problem, state the claim, preview Track 2 as the contribution.
5. **Related Work** — ROMA, R3DM, Zambaldi, QMIX/MAPPO. Keep this tight; the differentiation table in IDEA.md is already complete.
6. **Discussion** — connect behavioral role characteristics (Support uniform, Vanguard sensitive) to the structural mechanism.
7. **Abstract** — write last.

### Target Venues

| Venue | Type | Deadline Cycle | Notes |
| :--- | :--- | :--- | :--- |
| ICLR 2027 | Full paper | Oct 2026 | Competitive; Track 2 must be strong |
| AAMAS 2027 | Full paper | Oct 2026 | Natural fit for MARL mechanism paper |
| NeurIPS 2026 Workshop | Workshop | Sep 2026 | Good intermediate checkpoint |
| CoRL 2026 | Full paper | Jun 2026 | If UE5 appendix is prominent |

**Recommendation:** Target NeurIPS 2026 Workshop as a checkpoint to get reviewer feedback, then submit the full paper to ICLR 2027 or AAMAS 2027.

---

## Risk Register

| Risk | Probability | Impact | Mitigation |
| :--- | :--- | :--- | :--- |
| Track 2 shows no cross-role routing | Medium | High | Design map to force role dependency (Support must be adjacent to Strike to provide heal — creates structural necessity for Strike→Support attention) |
| Full ≈ Role+MLP in performance | Medium | Medium | Pivot contribution to interpretability/XAI framing — attention provides structured explanation even if performance is equivalent |
| SMACv2 unit types don't behave as clean roles | Medium | Medium | Use UE5 data as primary Track 2 environment; SMACv2 as Track 1 performance benchmark only |
| Condition 4 (shared, no label) performs close to Full | Low | High | If true, the label claim fails — revisit whether role_onehot is being zeroed correctly in ally_tok |
| ROMA outperforms Full in Phase 2 | Low | Medium | ROMA discovers roles dynamically; your contribution is the mechanism analysis, not SOTA performance |

---

## Project Timeline

```
Week 0      Start PoC implementation (AGENT_BRIEF.md spec)
Week 1      PoC training complete — 5 conditions × 2M steps
Week 2      Track 2 analysis complete — Go/No-Go decision
Week 3-4    SMACv2 porting + ablation training
Week 5-6    SMACv2 baseline comparisons (QMIX, MAPPO, ROMA)
Week 7      Track 2 replicated on SMACv2 — all figures generated
Week 8-10   Paper writing (order above)
Week 10     NeurIPS Workshop submission (draft checkpoint)
Week 12     Incorporate feedback — full paper draft
```

---

## File Index

| File | Purpose |
| :--- | :--- |
| `docs/IDEA.md` | Research idea, claims, theory, ablation design (source of truth) |
| `docs/validation/AGENT_BRIEF.md` | Full implementation spec for PoC agent |
| `docs/validation/PLAN.md` | This document — phased plan and milestones |

---

## Key Invariants (Do Not Change Between Conditions)

These must remain identical across all 5 ablation conditions to ensure valid comparison:

- Observation dimensionality (63 dims)
- Role-specific reward functions and α=0.2 team mixing ratio
- Training hyperparameters (lr, batch size, GAE λ, PPO ε)
- Episode length (200 steps)
- Map layout and role assignment logic
- Random seeds (use 3 seeds per condition, report mean ± std)
