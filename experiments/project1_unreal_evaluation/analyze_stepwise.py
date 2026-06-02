"""
Deeper analysis of the Stage 2 stepwise CSV: characterize team clustering with
richer statistics than the saturated binary duplicate rate.

Produces:
  - distribution of #unique objectives per team-step (assigned & nearest)
  - mean max-cluster size (most agents sharing one objective)
  - coverage vs normalized episode progress (early/mid/late thirds)
  - per-role contribution
Writes outputs/stepwise_analysis.json and prints a summary.
"""

import csv
import glob
import json
import os
from collections import defaultdict

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "outputs")


def load_rows():
    f = sorted(glob.glob(os.path.join(OUT, "unreal_eval_stepwise_*.csv")))[-1]
    rows = []
    with open(f) as fh:
        for r in csv.DictReader(fh):
            rows.append(r)
    return rows, f


def main():
    rows, f = load_rows()
    print(f"Loaded {len(rows)} rows from {os.path.basename(f)}")

    # Group by (env, episode, step) -> list of (role, assigned, nearest)
    groups = defaultdict(list)
    ep_maxstep = defaultdict(int)
    for r in rows:
        key = (int(r["env_id"]), int(r["episode"]), int(r["step"]))
        groups[key].append((r["role"], int(r["assigned_objective"]), int(r["nearest_objective"])))
        epk = (int(r["env_id"]), int(r["episode"]))
        ep_maxstep[epk] = max(ep_maxstep[epk], int(r["step"]))

    uniq_assigned, uniq_nearest = [], []
    cov_assigned, cov_nearest = [], []
    maxcluster_assigned, maxcluster_nearest = [], []
    nalive = []
    # coverage by episode-progress third
    third_cov_nearest = {0: [], 1: [], 2: []}

    for key, members in groups.items():
        env, ep, step = key
        a = [m[1] for m in members if m[1] >= 0]
        n = [m[2] for m in members if m[2] >= 0]
        k_alive = len(members)
        nalive.append(k_alive)
        n_obj = 5
        if a:
            ua = len(set(a))
            uniq_assigned.append(ua)
            cov_assigned.append(ua / min(k_alive, n_obj))
            maxcluster_assigned.append(max([a.count(x) for x in set(a)]))
        if n:
            un = len(set(n))
            uniq_nearest.append(un)
            cov_nearest.append(un / min(k_alive, n_obj))
            maxcluster_nearest.append(max([n.count(x) for x in set(n)]))
            ms = ep_maxstep[(env, ep)]
            frac = step / ms if ms > 0 else 0
            third = min(2, int(frac * 3))
            third_cov_nearest[third].append(un / min(k_alive, n_obj))

    def dist(vals, lo=1, hi=5):
        c = {k: 0 for k in range(lo, hi + 1)}
        for v in vals:
            c[v] = c.get(v, 0) + 1
        tot = len(vals)
        return {k: round(c[k] / tot, 4) for k in c}

    summary = {
        "n_team_steps": len(uniq_nearest),
        "mean_alive_per_step": round(float(np.mean(nalive)), 3),
        "nearest": {
            "mean_unique_objectives": round(float(np.mean(uniq_nearest)), 3),
            "mean_coverage": round(float(np.mean(cov_nearest)), 4),
            "mean_max_cluster_size": round(float(np.mean(maxcluster_nearest)), 3),
            "unique_obj_distribution": dist(uniq_nearest),
        },
        "assigned": {
            "mean_unique_objectives": round(float(np.mean(uniq_assigned)), 3),
            "mean_coverage": round(float(np.mean(cov_assigned)), 4),
            "mean_max_cluster_size": round(float(np.mean(maxcluster_assigned)), 3),
            "unique_obj_distribution": dist(uniq_assigned),
        },
        "coverage_nearest_by_episode_third": {
            "early": round(float(np.mean(third_cov_nearest[0])), 4),
            "mid": round(float(np.mean(third_cov_nearest[1])), 4),
            "late": round(float(np.mean(third_cov_nearest[2])), 4),
        },
    }

    with open(os.path.join(OUT, "stepwise_analysis.json"), "w") as fh:
        json.dump(summary, fh, indent=2)

    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
