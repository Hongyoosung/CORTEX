"""
Stage 1: Python synthetic attention probe.

Loads the three per-role EntityCentricPolicy checkpoints (Self+Cross attention),
runs them on controlled synthetic scenarios, extracts Self/Cross attention maps,
and computes the Stage-1 metrics defined in the experiment plan:

  - Padding suppression (mask works)
  - Self-Attention DeltaMax (Clustered vs Spread / Converged vs Dispersed)
  - Cross-Attention focus shift
  - Self-Cross consistency
  - Objective-preference shift (EQS proxy)
  - Duplicate-risk proxy: Self+Cross vs Cross-only (self-attn ablated)

Outputs:
  outputs/attention_probe_results.csv     per (role, scenario) attention/EQS rows
  outputs/attention_probe_metrics.json    structured metric summary
  outputs/raw_attention.npz               raw attention tensors for plotting
  outputs/attention_probe_summary.md      human-readable summary table

Usage:
  python run_attention_probe.py \
      --checkpoint ../../DE_Training/training_results/20260328_032546/best
"""

import argparse
import csv
import json
import os
from datetime import datetime

import numpy as np
import torch

import build_synthetic_obs as B
import metrics as M
from ckpt_loader import load_role_policies

ROLES = ["strike", "vanguard", "support"]
GROUPS = ["ally", "enemy", "base"]


def _scenario_obs(name, role):
    return B.CORE_SCENARIOS[name](role)


def run_probe(checkpoint, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    os.makedirs(os.path.join(out_dir, "figures"), exist_ok=True)

    print("=" * 64)
    print(f"Loading checkpoint: {checkpoint}")
    models = load_role_policies(checkpoint)
    if not models:
        raise SystemExit("No role policies loaded — check checkpoint path.")

    # results[role][scenario] = probe_forward dict (full model)
    results = {r: {} for r in models}
    results_ablated = {r: {} for r in models}
    raw_store = {}

    csv_rows = []

    for role, (model, _sd) in models.items():
        # Core scenarios
        for scen in B.CORE_SCENARIOS:
            obs = _scenario_obs(scen, role)
            full = M.probe_forward(model, obs, ablate_self=False)
            abl = M.probe_forward(model, obs, ablate_self=True)
            results[role][scen] = full
            results_ablated[role][scen] = abl

            pad_mean, pad_max = M.padding_suppression(full)
            row = {
                "role": role,
                "scenario": scen,
                "ablate_self": False,
                "pad_attn_mean": round(pad_mean, 6),
                "pad_attn_max": round(pad_max, 6),
            }
            for g in GROUPS:
                row[f"top_self_{g}"] = M.top_self_key(full, g)
                row[f"top_cross_{g}"] = M.top_cross_slot(full, g)
                row[f"selfcross_match_{g}"] = M.self_cross_consistency(full, g)
            for i, lbl in enumerate(M.EQS_LABELS):
                row[f"eqs_{lbl}"] = round(float(full["eqs"][i]), 5)
            csv_rows.append(row)

            # store raw for plotting
            for k, v in full.items():
                raw_store[f"{role}/{scen}/{k}"] = np.asarray(v)

        # Padding stress sweep
        for k in B.PADDING_STRESS_K:
            obs = B.scenario_padding_stress(role, k)
            full = M.probe_forward(model, obs, ablate_self=False)
            pad_mean, pad_max = M.padding_suppression(full)
            csv_rows.append({
                "role": role,
                "scenario": f"padding_stress_k{k}",
                "ablate_self": False,
                "pad_attn_mean": round(pad_mean, 6),
                "pad_attn_max": round(pad_max, 6),
            })
            raw_store[f"{role}/padding_stress_k{k}/ally_cross"] = full["ally_cross"]
            raw_store[f"{role}/padding_stress_k{k}/ally_mask"] = full["ally_mask"]

    # ── Aggregate metrics ────────────────────────────────────────────────────
    summary = {
        "run_timestamp": datetime.now().isoformat(timespec="seconds"),
        "checkpoint": os.path.abspath(checkpoint),
        "torch_version": torch.__version__,
        "numpy_version": np.__version__,
        "roles": list(models.keys()),
        "scenarios": list(B.CORE_SCENARIOS.keys()),
        "padding_stress_k": B.PADDING_STRESS_K,
        "per_role": {},
        "global": {},
    }

    all_pad_max = []
    consistency_hits, consistency_total = 0, 0

    for role in models:
        full = results[role]
        abl = results_ablated[role]
        rsum = {}

        # Padding suppression (max over all core scenarios)
        pad_maxes = [M.padding_suppression(full[s])[1] for s in B.CORE_SCENARIOS]
        rsum["padding_suppression_max"] = round(float(max(pad_maxes)), 6)
        all_pad_max.extend(pad_maxes)

        # Self-Attention DeltaMax
        rsum["ally_self_deltamax_clustered_vs_spread"] = round(
            M.self_attn_deltamax(full["ally_clustered"], full["ally_spread"], "ally"), 4
        )
        rsum["enemy_self_deltamax_converged_vs_dispersed"] = round(
            M.self_attn_deltamax(full["enemy_converged"], full["enemy_dispersed"], "enemy"), 4
        )

        # Cross-Attention focus shift (ally, clustered vs spread)
        top_c = M.top_cross_slot(full["ally_clustered"], "ally")
        top_s = M.top_cross_slot(full["ally_spread"], "ally")
        rsum["ally_cross_focus_shift_clustered_vs_spread"] = bool(top_c != top_s)
        rsum["ally_cross_top_clustered"] = top_c
        rsum["ally_cross_top_spread"] = top_s
        rsum["ally_cross_tv_shift_clustered_vs_spread"] = round(
            M.cross_attn_tv_shift(full["ally_clustered"], full["ally_spread"], "ally"), 4
        )
        rsum["enemy_cross_tv_shift_converged_vs_dispersed"] = round(
            M.cross_attn_tv_shift(full["enemy_converged"], full["enemy_dispersed"], "enemy"), 4
        )

        # Self-Cross consistency (mean over core scenarios x groups)
        hits = tot = 0
        for s in B.CORE_SCENARIOS:
            for g in GROUPS:
                hits += M.self_cross_consistency(full[s], g)
                tot += 1
        rsum["self_cross_consistency"] = round(hits / tot, 3)
        consistency_hits += hits
        consistency_total += tot

        # Objective-preference shift: AllyObjectiveProximity, clustered vs spread
        pref_clustered = M.crowded_objective_preference(full["ally_clustered"])
        pref_spread = M.crowded_objective_preference(full["ally_spread"])
        rsum["ally_obj_pref_clustered"] = round(pref_clustered, 5)
        rsum["ally_obj_pref_spread"] = round(pref_spread, 5)
        rsum["objective_preference_shift"] = round(pref_spread - pref_clustered, 5)

        # Duplicate-risk proxy: response = pref_spread - pref_clustered.
        # A crowding-aware model lowers the preference when allies are clustered,
        # so a larger (more positive) response means stronger duplicate avoidance.
        # Compare Self+Cross (full) vs Cross-only (self-attn ablated).
        resp_full = pref_spread - pref_clustered
        resp_abl = (
            M.crowded_objective_preference(abl["ally_spread"])
            - M.crowded_objective_preference(abl["ally_clustered"])
        )
        rsum["dup_risk_response_self_plus_cross"] = round(resp_full, 5)
        rsum["dup_risk_response_cross_only"] = round(resp_abl, 5)
        rsum["dup_risk_self_attn_gain"] = round(resp_full - resp_abl, 5)

        summary["per_role"][role] = rsum

    summary["global"]["padding_suppression_max"] = round(float(max(all_pad_max)), 6)
    summary["global"]["self_cross_consistency"] = round(consistency_hits / consistency_total, 3)

    # ── Write outputs ────────────────────────────────────────────────────────
    csv_path = os.path.join(out_dir, "attention_probe_results.csv")
    fieldnames = sorted({k for row in csv_rows for k in row})
    # put identifiers first
    lead = ["role", "scenario", "ablate_self", "pad_attn_mean", "pad_attn_max"]
    fieldnames = lead + [f for f in fieldnames if f not in lead]
    with open(csv_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for row in csv_rows:
            w.writerow(row)

    json_path = os.path.join(out_dir, "attention_probe_metrics.json")
    with open(json_path, "w") as f:
        json.dump(summary, f, indent=2)

    npz_path = os.path.join(out_dir, "raw_attention.npz")
    np.savez_compressed(npz_path, **raw_store)

    _write_summary_md(out_dir, summary)

    print("\nOutputs written:")
    print(f"  {csv_path}")
    print(f"  {json_path}")
    print(f"  {npz_path}")
    print(f"  {os.path.join(out_dir, 'attention_probe_summary.md')}")
    print("\n=== GLOBAL ===")
    print(f"  padding_suppression_max = {summary['global']['padding_suppression_max']}")
    print(f"  self_cross_consistency  = {summary['global']['self_cross_consistency']}")
    for role in models:
        print(f"\n=== {role.upper()} ===")
        for k, v in summary["per_role"][role].items():
            print(f"  {k:48s} {v}")

    return summary


def _write_summary_md(out_dir, summary):
    lines = []
    lines.append("# Stage 1 Attention Probe — Auto-generated Summary\n")
    lines.append(f"- Run: {summary['run_timestamp']}")
    lines.append(f"- Checkpoint: `{summary['checkpoint']}`")
    lines.append(f"- torch {summary['torch_version']}, numpy {summary['numpy_version']}\n")

    lines.append("## Global")
    lines.append("| Metric | Value |")
    lines.append("|---|---:|")
    lines.append(f"| Padding suppression (max attn on padded slot) | {summary['global']['padding_suppression_max']} |")
    lines.append(f"| Self-Cross consistency (all roles/groups) | {summary['global']['self_cross_consistency']} |\n")

    lines.append("## Per-role")
    hdr = [
        "Role", "Pad max", "Ally ΔMax (Clu/Spr)", "Enemy ΔMax (Cnv/Dsp)",
        "Cross focus shift", "Self-Cross consist.",
        "Obj pref shift (Spr-Clu)", "Dup-risk Self+Cross", "Dup-risk Cross-only", "Self-attn gain",
    ]
    lines.append("| " + " | ".join(hdr) + " |")
    lines.append("|" + "|".join(["---"] * len(hdr)) + "|")
    for role, r in summary["per_role"].items():
        lines.append("| " + " | ".join(str(x) for x in [
            role,
            r["padding_suppression_max"],
            r["ally_self_deltamax_clustered_vs_spread"],
            r["enemy_self_deltamax_converged_vs_dispersed"],
            r["ally_cross_focus_shift_clustered_vs_spread"],
            r["self_cross_consistency"],
            r["objective_preference_shift"],
            r["dup_risk_response_self_plus_cross"],
            r["dup_risk_response_cross_only"],
            r["dup_risk_self_attn_gain"],
        ]) + " |")
    with open(os.path.join(out_dir, "attention_probe_summary.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument(
        "--checkpoint",
        default=os.path.join(here, "..", "..", "DE_Training", "training_results",
                             "20260328_032546", "best"),
    )
    ap.add_argument("--out-dir", default=os.path.join(here, "outputs"))
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()

    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    run_probe(args.checkpoint, args.out_dir)


if __name__ == "__main__":
    main()
