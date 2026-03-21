"""
Self-Attention Weight 히트맵 시각화 스크립트.

학습된 체크포인트에서 Attention weight를 추출하여
엔티티 간 관계를 히트맵으로 시각화합니다.

Usage:
  # RLlib 체크포인트 (3역할 분리)
  python attention_heatmap.py --checkpoint ../training_results/20260320_120436/best

  # 특정 시나리오로 테스트
  python attention_heatmap.py --checkpoint ../training_results/20260320_120436/best --scenario clustered
"""

import os
import sys
import argparse
import numpy as np
import torch
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.font_manager as fm

sys.path.append(os.path.dirname(os.path.abspath(__file__)))

from policy import (
    EntityCentricPolicy, OBS_DIM, EQS_DIM, EQS_LABELS,
    SELF_DIM, ALLY_DIM, ENEMY_DIM, BASE_DIM,
    MAX_ALLIES, MAX_ENEMIES, MAX_BASES,
    SELF_START, ALLY_START, ENEMY_START, BASE_START,
    ALLY_MASK_START, ENEMY_MASK_START, BASE_MASK_START, STRATEGY_START,
)


# ── Attention hook ────────────────────────────────────────────────────────────

class AttentionExtractor:
    """Forward hook으로 Self-Attention / Cross-Attention weight를 캡처."""

    def __init__(self, model: EntityCentricPolicy):
        self.weights = {}
        self._hooks = []

        for name, module in [
            ("ally_self",  model.ally_self_attn),
            ("enemy_self", model.enemy_self_attn),
            ("base_self",  model.base_self_attn),
            ("ally_cross",  model.ally_attn),
            ("enemy_cross", model.enemy_attn),
            ("base_cross",  model.base_attn),
        ]:
            h = module.register_forward_hook(self._make_hook(name))
            self._hooks.append(h)

    def _make_hook(self, name):
        def hook(module, input, output):
            # MultiheadAttention returns (attn_output, attn_weight)
            # attn_weight: (B, tgt_len, src_len)
            self.weights[name] = output[1].detach().cpu()
        return hook

    def remove(self):
        for h in self._hooks:
            h.remove()


# ── 시나리오 생성 ─────────────────────────────────────────────────────────────

def make_scenario_clustered() -> torch.Tensor:
    """아군 3명이 같은 거점 근처에 밀집한 시나리오."""
    obs = torch.zeros(1, OBS_DIM)

    # Self token
    obs[0, 0:3] = torch.tensor([0.5, 0.5, 0.5])  # 위치
    obs[0, 3:6] = torch.tensor([0.0, 0.0, 0.0])  # 속도
    obs[0, 6]   = 1.0                              # 체력

    # 아군 3명: 거의 같은 위치 (밀집)
    for i in range(3):
        s = ALLY_START + i * ALLY_DIM
        obs[0, s:s+3] = torch.tensor([0.1 + i*0.01, 0.1, 0.0])  # 미세 차이
        obs[0, s+3] = 0.8 + i * 0.05  # health
        obs[0, s+4] = 1.0             # alive
        obs[0, s+5] = float(i == 0)   # is_assault
        obs[0, s+6] = float(i == 1)   # is_defend
        obs[0, s+7] = float(i == 2)   # is_support

    # 아군 4번: 멀리 떨어진 위치
    s = ALLY_START + 3 * ALLY_DIM
    obs[0, s:s+3] = torch.tensor([-0.4, -0.3, 0.0])
    obs[0, s+3] = 1.0
    obs[0, s+4] = 1.0
    obs[0, s+5] = 1.0  # assault

    # 아군 마스크: 0~3 유효, 4~7 패딩
    obs[0, ALLY_MASK_START:ALLY_MASK_START+4] = 0.0
    obs[0, ALLY_MASK_START+4:ENEMY_MASK_START] = 1.0

    # 적 2명
    for i in range(2):
        s = ENEMY_START + i * ENEMY_DIM
        obs[0, s:s+3] = torch.tensor([0.3 + i*0.1, -0.2, 0.0])
        obs[0, s+3] = 0.7
        obs[0, s+4] = 0.9

    obs[0, ENEMY_MASK_START:ENEMY_MASK_START+2] = 0.0
    obs[0, ENEMY_MASK_START+2:BASE_MASK_START] = 1.0

    # 거점 3개
    for i in range(3):
        s = BASE_START + i * BASE_DIM
        obs[0, s:s+2] = torch.tensor([0.1 * (i - 1), 0.2 * i])
        obs[0, s+2] = 0.3
        obs[0, s+3] = float(i == 0)  # 점유
        obs[0, s+4] = 0.5 * i        # 점령 진행도
        obs[0, s+5] = float(i == 1)  # 할당
        obs[0, s+6] = 0.5

    obs[0, BASE_MASK_START:BASE_MASK_START+3] = 0.0
    obs[0, BASE_MASK_START+3:STRATEGY_START] = 1.0

    # 전략: assault
    obs[0, STRATEGY_START] = 1.0

    return obs


def make_scenario_spread() -> torch.Tensor:
    """아군 4명이 각기 다른 거점 근처에 분산 배치된 시나리오."""
    obs = torch.zeros(1, OBS_DIM)

    obs[0, 0:3] = torch.tensor([0.0, 0.0, 0.5])
    obs[0, 6] = 1.0

    positions = [
        [0.3, 0.3, 0.0],
        [-0.3, 0.3, 0.0],
        [0.3, -0.3, 0.0],
        [-0.3, -0.3, 0.0],
    ]
    for i in range(4):
        s = ALLY_START + i * ALLY_DIM
        obs[0, s:s+3] = torch.tensor(positions[i])
        obs[0, s+3] = 1.0
        obs[0, s+4] = 1.0
        obs[0, s+5] = float(i < 2)   # assault
        obs[0, s+6] = float(i == 2)  # defend
        obs[0, s+7] = float(i == 3)  # support

    obs[0, ALLY_MASK_START:ALLY_MASK_START+4] = 0.0
    obs[0, ALLY_MASK_START+4:ENEMY_MASK_START] = 1.0

    for i in range(2):
        s = ENEMY_START + i * ENEMY_DIM
        obs[0, s:s+3] = torch.tensor([0.2 * (i*2 - 1), 0.0, 0.0])
        obs[0, s+3] = 0.8
        obs[0, s+4] = 0.9

    obs[0, ENEMY_MASK_START:ENEMY_MASK_START+2] = 0.0
    obs[0, ENEMY_MASK_START+2:BASE_MASK_START] = 1.0

    for i in range(3):
        s = BASE_START + i * BASE_DIM
        obs[0, s:s+2] = torch.tensor([0.3 * (i-1), 0.2])
        obs[0, s+2] = 0.3
        obs[0, s+3] = float(i == 0)
        obs[0, s+4] = 0.3 * i
        obs[0, s+5] = float(i == 1)
        obs[0, s+6] = 0.5

    obs[0, BASE_MASK_START:BASE_MASK_START+3] = 0.0
    obs[0, BASE_MASK_START+3:STRATEGY_START] = 1.0

    obs[0, STRATEGY_START] = 1.0

    return obs


SCENARIOS = {
    "clustered": make_scenario_clustered,
    "spread":    make_scenario_spread,
}


# ── 시각화 ────────────────────────────────────────────────────────────────────

def plot_self_attention_heatmaps(
    attn_weights: dict,
    scenario_name: str,
    role_name: str,
    output_dir: str,
):
    """Self-Attention weight를 8×8 히트맵으로 그립니다."""

    fig, axes = plt.subplots(1, 3, figsize=(18, 5))
    fig.suptitle(
        f"Self-Attention Weights — {role_name.upper()} / {scenario_name}",
        fontsize=14, fontweight="bold",
    )

    groups = [
        ("ally_self",  "Allies",  MAX_ALLIES),
        ("enemy_self", "Enemies", MAX_ENEMIES),
        ("base_self",  "Bases",   MAX_BASES),
    ]

    for ax, (key, label, max_n) in zip(axes, groups):
        w = attn_weights[key][0].numpy()  # (tgt, src) = (8, 8)

        im = ax.imshow(w, cmap="YlOrRd", vmin=0, vmax=w.max() + 1e-8, aspect="equal")
        ax.set_title(f"{label} Self-Attention", fontsize=12)
        ax.set_xlabel("Key (entity slot)")
        ax.set_ylabel("Query (entity slot)")
        ax.set_xticks(range(max_n))
        ax.set_yticks(range(max_n))

        # 수치 표시
        for i in range(max_n):
            for j in range(max_n):
                val = w[i, j]
                color = "white" if val > w.max() * 0.6 else "black"
                ax.text(j, i, f"{val:.2f}", ha="center", va="center",
                        fontsize=7, color=color)

        fig.colorbar(im, ax=ax, shrink=0.8)

    plt.tight_layout()
    fname = f"attn_self_{role_name}_{scenario_name}.png"
    path = os.path.join(output_dir, fname)
    plt.savefig(path, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"  Saved: {path}")
    return path


def plot_cross_attention_heatmaps(
    attn_weights: dict,
    scenario_name: str,
    role_name: str,
    output_dir: str,
):
    """Cross-Attention weight (1×8)를 바 차트로 그립니다."""

    fig, axes = plt.subplots(1, 3, figsize=(18, 4))
    fig.suptitle(
        f"Cross-Attention Weights (Self → Entities) — {role_name.upper()} / {scenario_name}",
        fontsize=14, fontweight="bold",
    )

    groups = [
        ("ally_cross",  "Allies",  MAX_ALLIES),
        ("enemy_cross", "Enemies", MAX_ENEMIES),
        ("base_cross",  "Bases",   MAX_BASES),
    ]

    for ax, (key, label, max_n) in zip(axes, groups):
        w = attn_weights[key][0, 0].numpy()  # (src,) = (8,)
        bars = ax.bar(range(max_n), w, color="steelblue")
        ax.set_title(f"Self → {label}", fontsize=12)
        ax.set_xlabel("Entity slot")
        ax.set_ylabel("Attention weight")
        ax.set_xticks(range(max_n))
        ax.set_ylim(0, max(w.max() * 1.2, 0.01))

        for bar, val in zip(bars, w):
            if val > 0.01:
                ax.text(bar.get_x() + bar.get_width()/2, bar.get_height(),
                        f"{val:.2f}", ha="center", va="bottom", fontsize=8)

    plt.tight_layout()
    fname = f"attn_cross_{role_name}_{scenario_name}.png"
    path = os.path.join(output_dir, fname)
    plt.savefig(path, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"  Saved: {path}")
    return path


def plot_scenario_comparison(
    weights_clustered: dict,
    weights_spread: dict,
    role_name: str,
    output_dir: str,
):
    """밀집 vs 분산 시나리오의 Ally Self-Attention 차이를 나란히 비교."""

    fig, axes = plt.subplots(1, 3, figsize=(20, 5))
    fig.suptitle(
        f"Ally Self-Attention Comparison — {role_name.upper()}",
        fontsize=14, fontweight="bold",
    )

    w_c = weights_clustered["ally_self"][0].numpy()
    w_s = weights_spread["ally_self"][0].numpy()
    w_diff = w_c - w_s

    for ax, (data, title, cmap) in zip(axes, [
        (w_c, "Clustered (allies at same base)", "YlOrRd"),
        (w_s, "Spread (allies at different bases)", "YlOrRd"),
        (w_diff, "Difference (Clustered - Spread)", "RdBu_r"),
    ]):
        vmax = max(abs(data.min()), abs(data.max())) + 1e-8
        if "Difference" in title:
            im = ax.imshow(data, cmap=cmap, vmin=-vmax, vmax=vmax, aspect="equal")
        else:
            im = ax.imshow(data, cmap=cmap, vmin=0, vmax=data.max() + 1e-8, aspect="equal")
        ax.set_title(title, fontsize=11)
        ax.set_xlabel("Key slot")
        ax.set_ylabel("Query slot")
        ax.set_xticks(range(MAX_ALLIES))
        ax.set_yticks(range(MAX_ALLIES))
        for i in range(MAX_ALLIES):
            for j in range(MAX_ALLIES):
                val = data[i, j]
                color = "white" if abs(val) > vmax * 0.5 else "black"
                ax.text(j, i, f"{val:.2f}", ha="center", va="center",
                        fontsize=7, color=color)
        fig.colorbar(im, ax=ax, shrink=0.8)

    plt.tight_layout()
    fname = f"attn_comparison_{role_name}.png"
    path = os.path.join(output_dir, fname)
    plt.savefig(path, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"  Saved: {path}")
    return path


# ── 체크포인트 로드 ───────────────────────────────────────────────────────────

def load_policies_from_rllib(checkpoint_dir: str) -> dict:
    """
    RLlib 체크포인트에서 역할별 EntityCentricPolicy를 추출합니다.
    Returns: {"assault": model, "defend": model, "support": model}
    """
    import ray
    from ray.tune.registry import register_env
    from ray.rllib.models import ModelCatalog

    # train.py의 RLlib wrapper 재사용
    from train import (
        EntityCentricRLlibModel, create_ppo_config,
        STRATEGY_POLICY_NAMES,
    )
    try:
        from env_wrapper import DEEntityCentricEnv
    except ImportError:
        from training.env_wrapper import DEEntityCentricEnv

    ray.init(ignore_reinit_error=True, include_dashboard=False, logging_level="ERROR")
    register_env("de_entity_centric", lambda cfg: DEEntityCentricEnv(**cfg))
    ModelCatalog.register_custom_model("entity_centric_model", EntityCentricRLlibModel)

    algo = create_ppo_config().build()
    algo.restore(os.path.abspath(checkpoint_dir))

    models = {}
    for policy_name in STRATEGY_POLICY_NAMES.values():
        role = policy_name.replace("_policy", "")
        rllib_policy = algo.get_policy(policy_name)
        if rllib_policy:
            model = rllib_policy.model.policy
            model.eval()
            models[role] = model

    algo.stop()
    ray.shutdown()
    return models


def load_policy_from_pt(checkpoint_path: str) -> dict:
    """단일 .pt 체크포인트에서 모델 로드."""
    model = EntityCentricPolicy()
    model.load_state_dict(torch.load(checkpoint_path, map_location="cpu"))
    model.eval()
    return {"default": model}


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Self-Attention Weight Heatmap Visualizer")
    parser.add_argument("--checkpoint", required=True,
                        help="RLlib checkpoint dir or .pt file")
    parser.add_argument("--scenario", choices=list(SCENARIOS.keys()) + ["all"], default="all",
                        help="Test scenario (default: all)")
    parser.add_argument("--output-dir", default=None,
                        help="Output directory for images (default: checkpoint_dir/attention_viz/)")
    args = parser.parse_args()

    # 출력 디렉토리
    if args.output_dir:
        out_dir = args.output_dir
    else:
        base = args.checkpoint if os.path.isdir(args.checkpoint) else os.path.dirname(args.checkpoint)
        out_dir = os.path.join(base, "attention_viz")
    os.makedirs(out_dir, exist_ok=True)

    # 모델 로드
    print("=" * 60)
    print("Loading checkpoint...")
    if args.checkpoint.endswith(".pt") or args.checkpoint.endswith(".pth"):
        models = load_policy_from_pt(args.checkpoint)
    else:
        models = load_policies_from_rllib(args.checkpoint)
    print(f"  Loaded {len(models)} model(s): {list(models.keys())}")

    # 시나리오 결정
    if args.scenario == "all":
        scenario_names = list(SCENARIOS.keys())
    else:
        scenario_names = [args.scenario]

    # 각 모델 × 각 시나리오에 대해 히트맵 생성
    for role_name, model in models.items():
        print(f"\n{'='*60}")
        print(f"Role: {role_name.upper()}")
        print(f"{'='*60}")

        all_scenario_weights = {}

        for scenario_name in scenario_names:
            print(f"\n  Scenario: {scenario_name}")
            obs = SCENARIOS[scenario_name]()

            # Attention weight 추출
            extractor = AttentionExtractor(model)
            with torch.no_grad():
                # need_weights=True가 기본값이므로 그대로 forward
                eqs_out = model(obs)
            extractor.remove()

            print(f"  EQS output: {eqs_out[0].numpy().round(3)}")
            for key, w in extractor.weights.items():
                print(f"    {key}: shape={list(w.shape)}, "
                      f"max={w.max():.3f}, min={w.min():.3f}")

            # Self-Attention 히트맵
            plot_self_attention_heatmaps(extractor.weights, scenario_name, role_name, out_dir)

            # Cross-Attention 바 차트
            plot_cross_attention_heatmaps(extractor.weights, scenario_name, role_name, out_dir)

            all_scenario_weights[scenario_name] = extractor.weights

        # 밀집 vs 분산 비교 (두 시나리오 모두 있을 때)
        if "clustered" in all_scenario_weights and "spread" in all_scenario_weights:
            print(f"\n  Generating comparison plot...")
            plot_scenario_comparison(
                all_scenario_weights["clustered"],
                all_scenario_weights["spread"],
                role_name,
                out_dir,
            )

    print(f"\n{'='*60}")
    print(f"All heatmaps saved to: {out_dir}")
    print(f"{'='*60}")


if __name__ == "__main__":
    main()
