# Idea Finalization v3.0

## Title
**"Structured Role Differentiation via Label-Conditioned Intra-Set Self-Attention in Cooperative MARL"**

*(Analysis of Role-to-Role Relational Routing Mechanisms through Role-Label Conditioned Intra-Set Self-Attention)*

---

## Core Claim (v3 — Final Defensible Version)

In heterogeneous cooperative MARL with role-specific rewards, **Intra-Set Self-Attention utilizes role labels as direct routing signals for entity selection more effectively than MLP policies** — because role labels condition the Query (self) and Key/Value (ally) projections simultaneously, enabling role-to-role relational routing that MLP cannot structurally replicate.

**Three-part claim:**
1. Role labels in `self_tok` condition Q → the agent's own role biases which entities it attends to
2. Role labels in `ally_tok` condition K/V → attention can route based on *teammate roles*, not just positions
3. This cross-role relational routing produces qualitatively distinct, interpretable attention patterns per role — empirically absent in MLP baselines

---

## Comparison of Claims: v1 → v2 → v3

| Item | v1 (Discarded) | v2 (Previous) | v3 (Final) |
| :--- | :--- | :--- | :--- |
| **Reward Condition** | Identical rewards only | Role-specific + labels | Role-specific + labels |
| **Key Argument** | Attention alone creates roles | Attention structures labels better than MLP | Attention enables *role-to-role relational routing* — structurally impossible in MLP |
| **Theoretical Strength** | Exaggerated, falsified by project | Conservative, defensible | Precise mechanism with Q/K/V attribution |
| **Proof Method** | Performance only | Performance + visualization | Performance + visualization + cross-role slot analysis |
| **"No Role Label" ablation** | Independent policy (weak) | Independent policy (weak) | **Shared policy** (meaningful isolation) |

---

## Differentiation from Existing Research

| Research | Approach | Difference from This Paper |
| :--- | :--- | :--- |
| **ROMA (ICML 2020)** | Automatic role discovery | Pre-defined roles; analyzes the mechanism of how labels modulate Attention |
| **R3DM (ICML 2025)** | Attention-based role discovery + diversity | Analyzes structural causes of label-conditioned differentiation, not discovery |
| **Zambaldi (2018)** | Single-agent, Atari | Extension to Multi-Agent + heterogeneous role environments |
| **QMIX / MAPPO vanilla** | Cooperative optimization, no interpretability | Empirically visualizes role-specific Attention differentiation and cross-role routing |

---

## Theoretical Basis

### 1. Why MLP struggles with structured differentiation

In an MLP, the role label is concatenated as one element of the input:

$$h = \text{MLP}([o_{\text{entity}}, \text{role\_onehot}])$$

The label is processed identically to all other features. The logic of "which entity to focus on given my role" is not structurally encoded — it must be implicitly learned as a feature interaction buried in weight matrices. This path is opaque and difficult to attribute.

### 2. Why Self-Attention provides a direct routing path

Role labels appear in two places in the observation:
- `self_tok` (dim [215:218]): own role one-hot → encoded into **Q**
- `ally_tok` (dim [7:71]): each ally's role one-hot → encoded into **K/V**

The attention score between agent $i$ and ally $j$ is:

$$\text{attn}_{ij} = \text{softmax}\left(\frac{(W_Q \cdot x_{\text{self}})(W_K \cdot x_j)^T}{\sqrt{d}}\right)$$

Because $x_{\text{self}}$ contains the agent's own role and $x_j$ contains ally $j$'s role, **the similarity score is a function of the role-pair $(r_i, r_j)$, not just spatial features**. This means the network can learn role-to-role routing — e.g., Strike agents learn to weight Support ally slots higher when computing spatial context.

> **Reviewer pre-emption:** This does not mean MLP routing is impossible. $W_Q$ could theoretically learn to project the role dimension into entity selection logic. The claim is that attention provides a *more direct structural inductive bias* for this computation. Track 2 experiments empirically verify whether this bias actually activates.

### 3. Testable prediction from the theory

If role-to-role routing is active:
- Strike's self-attention weights over ally slots should differ between scenarios where a Support ally is present vs. absent
- Vanguard's weights should be most sensitive to frontline ally positions (highest Δmax)
- Support's weights should be uniformly distributed across all ally slots regardless of formation (role-consistent with monitoring all allies)

These are the exact patterns observed in the existing UE5 experiment data (Strike Δmax≈0.19, Vanguard highest, Support lowest). The PoC and SMACv2 experiments confirm or generalize this.

---

## Methodology

- **Algorithm:** MAPPO
- **Policies:** 3 independent role policies (Strike / Vanguard / Support) — *plus one shared policy for the "No Role Label" ablation condition*
- **Observation:** Entity-Centric vector — role labels as one-hot in `self_tok` and `ally_tok`
- **Network:** Intra-Set Self-Attention → Cross-Attention (2-stage pipeline)
- **Reward:** Role-specific individual rewards (Attack / Capture / Heal) + team reward mixing (α=0.2)

---

## Ablation Design (5 Conditions)

| # | Condition | Role Label | Architecture | Policy | Purpose |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | **Full (Proposed)** | ✅ | Self-Attn + Cross-Attn | Independent | Primary system |
| 2 | **No Self-Attn** | ✅ | MLP encoder + Cross-Attn | Independent | Isolates intra-set relational structure contribution |
| 3 | **Role Label + Pure MLP** ⭐ | ✅ | Full MLP (no attention) | Independent | **Core comparison:** label signal with no structural routing |
| 4 | **No Role Label** | ❌ | Self-Attn + Cross-Attn | **Shared** | Isolates label signal contribution — shared policy required to make removal meaningful |
| 5 | **Flat MLP** | ❌ | Full MLP (no attention) | Independent | Baseline |

**Architecture distinction for conditions 2 and 3:**
- Condition 2: Intra-Set Self-Attention block replaced with MLP; Cross-Attention still present
- Condition 3: All attention removed; pure MLP encoder throughout

**Why condition 4 requires a shared policy:** With independent policies, each network is already specialized by training exclusively on one role's trajectories. Removing the role label from observations would not create genuine role ambiguity — the policy is still role-specific by construction. Only a shared policy (one network, all roles) makes the "No Role Label" condition a meaningful test of whether the label is load-bearing.

---

## Validation Methods

### Track 1 — Quantitative Performance

- Compare Win Rate and convergence speed across all 5 conditions
- **Expected order:** `Full > No_SelfAttn > Role+MLP > No_RoleLabel > Flat_MLP`
- **Interpretation:**
  - If `Full >> Role+MLP`: structural routing contribution of attention is proven
  - If `Full ≈ Role+MLP`: contribution is interpretability, not performance — pivot framing to XAI direction (still publishable)
  - If `No_RoleLabel (shared) ≈ Full`: labels are not load-bearing — claim fails, revisit

### Track 2 — Role-to-Role Routing Analysis (Primary Contribution)

**Scenario comparison:** Clustered (allies grouped near same base) vs. Spread (allies at map corners)

**Measurements:**
1. Self-attention weight distribution per role across ally slots — Δmax as formation sensitivity metric
2. **Cross-role slot analysis (new):** For each role, track attention weight specifically toward ally slots containing a particular role class. E.g., does Strike attend more to Support ally slots?
3. MLP baseline comparison via gradient attribution or probing classifier — verify that role differentiation is structurally absent or less organized in condition 3
4. Attention heatmaps for Clustered vs. Spread for all three roles

**Claim is confirmed if:**
- Role-specific weight patterns are distinct and formation-responsive (existing UE5 data already shows this)
- Pattern is absent or unstructured in Role+MLP condition
- Cross-role slot weights align with domain logic (Strike→Support routing; Vanguard→frontline ally routing)

---

## Experimental Environments

| Phase | Environment | Purpose |
| :--- | :--- | :--- |
| **PoC** | MiniGrid (current code) | Verify attention differentiation pattern existence in minimal setting |
| **Main** | SMACv2 | Standard benchmark, reproducibility — note: SMACv2 unit types serve as heterogeneous roles |
| **Appendix** | UE5 DE project | Real-world game engine validation; existing attention data provides preliminary evidence |

> **SMACv2 note:** SMACv2 does not have the same 3-class Strike/Vanguard/Support structure. Map this to unit-type heterogeneity (Stalker/Zealot/Colossus in protoss maps). The role label becomes unit type one-hot; the mechanism claim transfers directly.

---

## Paper Structure

| Section | Content |
| :--- | :--- |
| **Introduction** | Reward engineering for heterogeneous cooperation is ambiguous. Attention provides structural routing that reward design cannot encode. Claim stated. |
| **Related Work** | ROMA / R3DM (discovery vs. conditioned analysis), Zambaldi (single-agent baseline), QMIX/MAPPO (no interpretability). |
| **Method** | Entity-centric observation layout, role label positions in self/ally tokens, 2-stage attention pipeline, 5-condition ablation setup. |
| **Theory** | MLP opacity argument, Q/K/V role-pair routing derivation, testable predictions per role. |
| **Experiments** | Track 1 (5-condition performance), Track 2 (cross-role slot analysis + heatmaps + MLP gradient attribution comparison). |
| **Discussion** | Connect Support uniform distribution / Vanguard high sensitivity / Strike medium sensitivity to role-pair routing hypothesis. |

---

## Key Risks and Mitigations

| Risk | Mitigation |
| :--- | :--- |
| **Weak differentiation in Track 2** | Design scenarios with sharper role division of labor (force role dependency in map layout) |
| **Full ≈ Role+MLP in Track 1** | Reframe contribution as interpretability / XAI — attention provides structured explanation even if performance is equivalent |
| **Reviewer: "Independent policies explain differentiation"** | Condition 4 (shared policy, no label) directly addresses this — if shared policy without labels fails, the label is proven necessary beyond policy separation |
| **SMACv2 unit types are not clean roles** | Limit SMACv2 analysis to maps with clear unit-type heterogeneity; keep UE5 as primary environment for Track 2 |
| **W_Q ignores role dimension** | Track 2 is the empirical proof — if routing is absent, the claim narrows to interpretability only |

---

## Roadmap

| Timeline | Milestone |
| :--- | :--- |
| **Now** | PoC (MiniGrid) — verify attention differentiation exists under controlled conditions |
| **+2 weeks** | SMACv2 porting + train all 5 ablation conditions |
| **+5 weeks** | Baseline comparisons (QMIX, MAPPO vanilla, ROMA) + generate Track 2 visualizations including cross-role slot analysis |
| **+8 weeks** | Paper writing — calibrate theory section depth based on Track 2 cross-role routing results |
