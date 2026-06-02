"""
Plot Stage 2 behavioural-evaluation metrics from the latest
unreal_eval_metrics_*.json + stepwise_analysis.json.

Figures (outputs/figures/):
  fig1_behavior_metrics.png  — coverage, mean unique objectives, max cluster size
  fig2_clustering_profile.png — #unique-objective distribution + coverage over episode thirds
  fig3_episode_outcomes.png  — win/loss + episode reward distribution
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


def _load_json(pattern_or_name):
    if pattern_or_name.endswith(".json") and os.path.exists(os.path.join(OUT, pattern_or_name)):
        with open(os.path.join(OUT, pattern_or_name)) as f:
            return json.load(f)
    files = sorted(glob.glob(os.path.join(OUT, pattern_or_name)))
    if not files:
        raise SystemExit(f"No file matching {pattern_or_name}")
    with open(files[-1]) as f:
        return json.load(f)


def fig_behavior(m, sa):
    fig, axes = plt.subplots(1, 3, figsize=(16, 4.5))
    defs = ["assigned", "nearest"]

    ax = axes[0]
    vals = [m["metrics"]["objective_coverage_assigned"], m["metrics"]["objective_coverage_nearest"]]
    bars = ax.bar(defs, vals, color=["slategray", "steelblue"])
    ax.axhline(1.0, color="green", ls="--", lw=1, label="ideal (all distinct)")
    ax.set_title("Objective Coverage")
    ax.set_ylabel("unique / min(alive, objectives)")
    ax.set_ylim(0, 1.05); ax.legend()
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width() / 2, v, f"{v:.3f}", ha="center", va="bottom")

    ax = axes[1]
    vals = [sa["assigned"]["mean_unique_objectives"], sa["nearest"]["mean_unique_objectives"]]
    bars = ax.bar(defs, vals, color=["slategray", "steelblue"])
    ax.axhline(5, color="green", ls="--", lw=1, label="5 objectives")
    ax.set_title("Mean Unique Objectives Covered (of 5)")
    ax.set_ylabel("count"); ax.set_ylim(0, 5.3); ax.legend()
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width() / 2, v, f"{v:.2f}", ha="center", va="bottom")

    ax = axes[2]
    vals = [sa["assigned"]["mean_max_cluster_size"], sa["nearest"]["mean_max_cluster_size"]]
    bars = ax.bar(defs, vals, color=["indianred", "darkorange"])
    ax.axhline(1, color="green", ls="--", lw=1, label="ideal (no pile-up)")
    ax.set_title("Mean Max-Cluster Size (agents on busiest objective)")
    ax.set_ylabel("agents"); ax.set_ylim(0, 5.3); ax.legend()
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width() / 2, v, f"{v:.2f}", ha="center", va="bottom")

    fig.suptitle(f"Stage 2 Behavioural Metrics — {m['episodes']} episodes, {m['num_envs']} envs, "
                 f"{sa['n_team_steps']} team-steps", fontsize=13, fontweight="bold")
    plt.tight_layout(rect=[0, 0, 1, 0.95])
    p = os.path.join(FIG, "fig1_behavior_metrics.png")
    plt.savefig(p, dpi=150, bbox_inches="tight"); plt.close()
    return p


def fig_clustering(sa):
    fig, axes = plt.subplots(1, 2, figsize=(13, 4.5))

    ax = axes[0]
    d = sa["nearest"]["unique_obj_distribution"]
    ks = sorted(int(k) for k in d)
    vals = [d[str(k)] for k in ks]
    bars = ax.bar(ks, vals, color="steelblue")
    ax.set_title("Distribution of #Unique Objectives per Team-Step (nearest)")
    ax.set_xlabel("unique objectives covered (5 agents)")
    ax.set_ylabel("fraction of team-steps")
    for b, v in zip(bars, vals):
        if v > 0.005:
            ax.text(b.get_x() + b.get_width() / 2, v, f"{v:.2f}", ha="center", va="bottom")

    ax = axes[1]
    t = sa["coverage_nearest_by_episode_third"]
    xs = ["early", "mid", "late"]
    vals = [t[x] for x in xs]
    ax.plot(xs, vals, marker="o", color="seagreen", lw=2)
    ax.set_title("Objective Coverage over Episode Progress (nearest)")
    ax.set_ylabel("mean coverage"); ax.set_ylim(0, 1.0)
    for x, v in zip(xs, vals):
        ax.text(x, v + 0.02, f"{v:.3f}", ha="center")

    fig.suptitle("Team Clustering Profile", fontsize=13, fontweight="bold")
    plt.tight_layout(rect=[0, 0, 1, 0.95])
    p = os.path.join(FIG, "fig2_clustering_profile.png")
    plt.savefig(p, dpi=150, bbox_inches="tight"); plt.close()
    return p


def fig_outcomes(m):
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.5))
    ax = axes[0]
    bd = m["episode_breakdown"]
    keys = ["win", "loss", "draw", "timeout"]
    vals = [bd[k] for k in keys]
    bars = ax.bar(keys, vals, color=["seagreen", "indianred", "goldenrod", "gray"])
    ax.set_title(f"Episode Outcomes vs Scripted AI (win rate {m['metrics']['win_rate']:.0%})")
    ax.set_ylabel("episodes")
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width() / 2, v, str(v), ha="center", va="bottom")

    ax = axes[1]
    rewards = m.get("episode_rewards", [])
    if rewards:
        ax.hist(rewards, bins=10, color="mediumpurple", edgecolor="black")
        ax.axvline(float(np.mean(rewards)), color="red", ls="--",
                   label=f"mean={np.mean(rewards):.1f}")
        ax.legend()
    ax.set_title("Episode Reward Distribution")
    ax.set_xlabel("episode reward (team total)"); ax.set_ylabel("count")

    plt.tight_layout()
    p = os.path.join(FIG, "fig3_episode_outcomes.png")
    plt.savefig(p, dpi=150, bbox_inches="tight"); plt.close()
    return p


def main():
    os.makedirs(FIG, exist_ok=True)
    m = _load_json("unreal_eval_metrics_*.json")
    sa = _load_json("stepwise_analysis.json")
    print("Figures:")
    print(f"  {fig_behavior(m, sa)}")
    print(f"  {fig_clustering(sa)}")
    print(f"  {fig_outcomes(m)}")


if __name__ == "__main__":
    main()
