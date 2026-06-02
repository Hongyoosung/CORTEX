"""
Generate portfolio figures for the Stage 1 attention probe from raw_attention.npz
and attention_probe_metrics.json.

Figures (saved to outputs/figures/):
  fig1_self_attention_clustered_vs_spread.png  — ally Self-Attention heatmaps + diff, per role
  fig2_cross_attention_shift.png               — ally Cross-Attention bars (clustered vs spread)
  fig3_padding_suppression.png                 — padded-slot attention across the k-sweep
  fig4_metric_summary.png                      — bar charts of DeltaMax / consistency / dup-risk

Usage:
  python plot_attention_probe.py
"""

import json
import os

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "outputs")
FIG = os.path.join(OUT, "figures")
ROLES = ["strike", "vanguard", "support"]


def _load():
    raw = np.load(os.path.join(OUT, "raw_attention.npz"))
    with open(os.path.join(OUT, "attention_probe_metrics.json")) as f:
        metrics = json.load(f)
    return raw, metrics


def _valid_n(mask):
    return int((mask < 0.5).sum())


def fig_self_attention(raw):
    """3 rows (roles) x 3 cols (clustered / spread / diff) ally self-attention."""
    fig, axes = plt.subplots(len(ROLES), 3, figsize=(15, 5 * len(ROLES)))
    for r, role in enumerate(ROLES):
        c = raw[f"{role}/ally_clustered/ally_self"]
        s = raw[f"{role}/ally_spread/ally_self"]
        m = raw[f"{role}/ally_clustered/ally_mask"]
        n = _valid_n(m)
        c, s = c[:n, :n], s[:n, :n]
        diff = c - s
        panels = [
            (c, f"{role} — Clustered", "YlOrRd", 0, max(c.max(), 1e-8)),
            (s, f"{role} — Spread", "YlOrRd", 0, max(s.max(), 1e-8)),
            (diff, f"{role} — Diff (Clu - Spr)", "RdBu_r", None, None),
        ]
        for ci, (data, title, cmap, vmin, vmax) in enumerate(panels):
            ax = axes[r, ci] if len(ROLES) > 1 else axes[ci]
            if cmap == "RdBu_r":
                vext = max(abs(data.min()), abs(data.max()), 1e-8)
                im = ax.imshow(data, cmap=cmap, vmin=-vext, vmax=vext)
            else:
                im = ax.imshow(data, cmap=cmap, vmin=vmin, vmax=vmax)
            ax.set_title(title, fontsize=11)
            ax.set_xlabel("Key (ally slot)")
            ax.set_ylabel("Query (ally slot)")
            ax.set_xticks(range(n))
            ax.set_yticks(range(n))
            for i in range(n):
                for j in range(n):
                    ax.text(j, i, f"{data[i, j]:.2f}", ha="center", va="center", fontsize=8)
            fig.colorbar(im, ax=ax, shrink=0.8)
    fig.suptitle("Ally Intra-Set Self-Attention: Clustered vs Spread", fontsize=15, fontweight="bold")
    plt.tight_layout(rect=[0, 0, 1, 0.98])
    p = os.path.join(FIG, "fig1_self_attention_clustered_vs_spread.png")
    plt.savefig(p, dpi=150, bbox_inches="tight")
    plt.close()
    return p


def fig_cross_attention(raw):
    fig, axes = plt.subplots(1, len(ROLES), figsize=(6 * len(ROLES), 4.5))
    for r, role in enumerate(ROLES):
        ax = axes[r]
        c = raw[f"{role}/ally_clustered/ally_cross"]
        s = raw[f"{role}/ally_spread/ally_cross"]
        m = raw[f"{role}/ally_clustered/ally_mask"]
        n = _valid_n(m)
        x = np.arange(n)
        w = 0.38
        ax.bar(x - w / 2, c[:n], w, label="Clustered", color="indianred")
        ax.bar(x + w / 2, s[:n], w, label="Spread", color="steelblue")
        ax.set_title(f"{role}: Self→Ally Cross-Attention", fontsize=11)
        ax.set_xlabel("Ally slot")
        ax.set_ylabel("Attention weight")
        ax.set_xticks(x)
        ax.legend()
    fig.suptitle("Cross-Attention Focus Shift (Self token → allies)", fontsize=14, fontweight="bold")
    plt.tight_layout(rect=[0, 0, 1, 0.95])
    p = os.path.join(FIG, "fig2_cross_attention_shift.png")
    plt.savefig(p, dpi=150, bbox_inches="tight")
    plt.close()
    return p


def fig_padding(raw):
    """Max attention assigned to any padded ally slot across the k-sweep."""
    ks = [0, 1, 2, 3, 4]
    fig, ax = plt.subplots(figsize=(8, 4.5))
    for role in ROLES:
        ymax = []
        for k in ks:
            key = f"{role}/padding_stress_k{k}/ally_cross"
            mkey = f"{role}/padding_stress_k{k}/ally_mask"
            if key not in raw:
                ymax.append(0.0)
                continue
            cross = raw[key]
            mask = raw[mkey] > 0.5
            ymax.append(float(cross[mask].max()) if mask.any() else 0.0)
        ax.plot(ks, ymax, marker="o", label=role)
    ax.set_title("Padding Suppression: max attention on padded ally slots", fontsize=12)
    ax.set_xlabel("Number of valid allies (k)")
    ax.set_ylabel("Max attention on padded slot")
    ax.set_ylim(-0.01, 0.05)
    ax.axhline(0.0, color="gray", lw=0.8, ls="--")
    ax.set_xticks(ks)
    ax.legend()
    plt.tight_layout()
    p = os.path.join(FIG, "fig3_padding_suppression.png")
    plt.savefig(p, dpi=150, bbox_inches="tight")
    plt.close()
    return p


def fig_metric_summary(metrics):
    pr = metrics["per_role"]
    roles = list(pr.keys())
    x = np.arange(len(roles))

    fig, axes = plt.subplots(1, 3, figsize=(18, 5))

    # (a) Self-Attention DeltaMax
    ax = axes[0]
    ally_dm = [pr[r]["ally_self_deltamax_clustered_vs_spread"] for r in roles]
    enemy_dm = [pr[r]["enemy_self_deltamax_converged_vs_dispersed"] for r in roles]
    w = 0.38
    ax.bar(x - w / 2, ally_dm, w, label="Ally (Clu vs Spr)", color="indianred")
    ax.bar(x + w / 2, enemy_dm, w, label="Enemy (Cnv vs Dsp)", color="darkorange")
    ax.set_title("Self-Attention DeltaMax (formation sensitivity)")
    ax.set_xticks(x); ax.set_xticklabels(roles); ax.set_ylabel("DeltaMax")
    ax.legend()

    # (b) Self-Cross consistency
    ax = axes[1]
    cons = [pr[r]["self_cross_consistency"] for r in roles]
    ax.bar(x, cons, color="seagreen")
    ax.set_title("Self-Cross Consistency")
    ax.set_xticks(x); ax.set_xticklabels(roles); ax.set_ylim(0, 1)
    ax.set_ylabel("Top-slot match rate")
    for i, v in enumerate(cons):
        ax.text(i, v + 0.02, f"{v:.2f}", ha="center")

    # (c) Duplicate-risk response: Self+Cross vs Cross-only
    ax = axes[2]
    full = [pr[r]["dup_risk_response_self_plus_cross"] for r in roles]
    abl = [pr[r]["dup_risk_response_cross_only"] for r in roles]
    ax.bar(x - w / 2, full, w, label="Self+Cross", color="mediumpurple")
    ax.bar(x + w / 2, abl, w, label="Cross-only (ablated)", color="silver")
    ax.set_title("Duplicate-Risk Response (Spread - Clustered pref)")
    ax.set_xticks(x); ax.set_xticklabels(roles); ax.set_ylabel("Obj-preference response")
    ax.axhline(0.0, color="gray", lw=0.8)
    ax.legend()

    fig.suptitle("Stage 1 Attention Probe — Metric Summary", fontsize=15, fontweight="bold")
    plt.tight_layout(rect=[0, 0, 1, 0.95])
    p = os.path.join(FIG, "fig4_metric_summary.png")
    plt.savefig(p, dpi=150, bbox_inches="tight")
    plt.close()
    return p


def main():
    os.makedirs(FIG, exist_ok=True)
    raw, metrics = _load()
    paths = [
        fig_self_attention(raw),
        fig_cross_attention(raw),
        fig_padding(raw),
        fig_metric_summary(metrics),
    ]
    print("Figures written:")
    for p in paths:
        print(f"  {p}")


if __name__ == "__main__":
    main()
