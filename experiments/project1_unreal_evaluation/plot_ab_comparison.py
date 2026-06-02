"""
A/B comparison figure: Self+Cross vs Cross-only (trained baseline).
Reads metrics + stepwise_analysis for both runs and draws a side-by-side summary.

  Self+Cross : outputs/unreal_eval_metrics_*.json        + outputs/stepwise_analysis.json
  Cross-only : outputs/crossonly/unreal_eval_metrics_*.json + outputs/crossonly/stepwise_analysis.json
"""
import glob
import json
import os

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "outputs")
FIG = os.path.join(OUT, "figures")


def _load(d):
    m = sorted(glob.glob(os.path.join(d, "unreal_eval_metrics_*.json")))[-1]
    with open(m) as f:
        metrics = json.load(f)
    with open(os.path.join(d, "stepwise_analysis.json")) as f:
        sa = json.load(f)
    return metrics, sa


def main():
    os.makedirs(FIG, exist_ok=True)
    m_sc, sa_sc = _load(OUT)                       # Self+Cross
    m_co, sa_co = _load(os.path.join(OUT, "crossonly"))  # Cross-only

    labels = ["Cross-only\n(no Self-Attn)", "Self+Cross"]
    colors = ["silver", "mediumpurple"]

    fig, axes = plt.subplots(1, 4, figsize=(20, 5))

    # 1. Objective coverage (nearest)
    ax = axes[0]
    vals = [sa_co["nearest"]["mean_coverage"], sa_sc["nearest"]["mean_coverage"]]
    b = ax.bar(labels, vals, color=colors)
    ax.set_title("Objective Coverage (nearest)")
    ax.set_ylabel("unique / min(alive, objectives)"); ax.set_ylim(0, 0.6)
    for bb_, v in zip(b, vals): ax.text(bb_.get_x()+bb_.get_width()/2, v, f"{v:.3f}", ha="center", va="bottom")

    # 2. Mean unique objectives
    ax = axes[1]
    vals = [sa_co["nearest"]["mean_unique_objectives"], sa_sc["nearest"]["mean_unique_objectives"]]
    b = ax.bar(labels, vals, color=colors)
    ax.set_title("Mean Unique Objectives (of 5)")
    ax.set_ylabel("count"); ax.set_ylim(0, 3)
    for bb_, v in zip(b, vals): ax.text(bb_.get_x()+bb_.get_width()/2, v, f"{v:.2f}", ha="center", va="bottom")

    # 3. Max cluster size (lower better)
    ax = axes[2]
    vals = [sa_co["nearest"]["mean_max_cluster_size"], sa_sc["nearest"]["mean_max_cluster_size"]]
    b = ax.bar(labels, vals, color=["indianred", "mediumpurple"])
    ax.set_title("Max-Cluster Size (lower = less pile-up)")
    ax.set_ylabel("agents on busiest objective"); ax.set_ylim(0, 5)
    for bb_, v in zip(b, vals): ax.text(bb_.get_x()+bb_.get_width()/2, v, f"{v:.2f}", ha="center", va="bottom")

    # 4. Win rate
    ax = axes[3]
    vals = [m_co["metrics"]["win_rate"], m_sc["metrics"]["win_rate"]]
    b = ax.bar(labels, vals, color=["silver", "seagreen"])
    ax.set_title("Win Rate vs Scripted AI")
    ax.set_ylabel("win rate"); ax.set_ylim(0, 0.4)
    for bb_, v in zip(b, vals): ax.text(bb_.get_x()+bb_.get_width()/2, v, f"{v:.3f}", ha="center", va="bottom")

    fig.suptitle("Trained A/B: Cross-only vs Self+Cross (both 66 iters, 24-ep eval) — Self-Attention effect",
                 fontsize=14, fontweight="bold")
    plt.tight_layout(rect=[0, 0, 1, 0.94])
    p = os.path.join(FIG, "fig4_ab_self_attn_effect.png")
    plt.savefig(p, dpi=150, bbox_inches="tight"); plt.close()
    print(f"Wrote {p}")

    # Print comparison table to stdout
    def line(name, co, sc, fmt="{:.3f}"):
        delta = ("+" if sc - co >= 0 else "") + fmt.format(sc - co)
        print(f"  {name:32s} {fmt.format(co):>10s} {fmt.format(sc):>10s} {delta:>11s}")
    print("\n  metric                            Cross-only  Self+Cross      delta")
    line("coverage_nearest", sa_co["nearest"]["mean_coverage"], sa_sc["nearest"]["mean_coverage"])
    line("coverage_assigned", sa_co["assigned"]["mean_coverage"], sa_sc["assigned"]["mean_coverage"])
    line("mean_unique_nearest", sa_co["nearest"]["mean_unique_objectives"], sa_sc["nearest"]["mean_unique_objectives"], "{:.2f}")
    line("max_cluster_nearest", sa_co["nearest"]["mean_max_cluster_size"], sa_sc["nearest"]["mean_max_cluster_size"], "{:.2f}")
    line("pct_all_on_1_obj", sa_co["nearest"]["unique_obj_distribution"]["1"], sa_sc["nearest"]["unique_obj_distribution"]["1"])
    line("win_rate", m_co["metrics"]["win_rate"], m_sc["metrics"]["win_rate"])
    line("mean_episode_reward", m_co["metrics"]["mean_episode_reward"], m_sc["metrics"]["mean_episode_reward"], "{:.1f}")


if __name__ == "__main__":
    main()
