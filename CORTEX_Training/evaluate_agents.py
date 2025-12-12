"""
Agent Evaluation Script for AlphaZero-Inspired Multi-Agent Combat AI

Compares trained agents against baselines:
- v3.0 (AlphaZero-inspired) vs v2.0 (hand-crafted heuristics)
- Metrics: Win rate, coordination, tactical efficiency, MCTS search efficiency

Requires game outcome data from UE5 with labeled agent versions.

Usage:
    python evaluate_agents.py --data ./evaluation_data --baseline v2.0 --trained v3.0

Requirements:
    pip install numpy pandas matplotlib seaborn
"""

import argparse
import json
import numpy as np
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Tuple
from collections import defaultdict
import statistics

try:
    import pandas as pd
except ImportError:
    print("Warning: pandas not installed. Install with: pip install pandas")
    pd = None

try:
    import matplotlib.pyplot as plt
    import seaborn as sns
    sns.set_theme(style="whitegrid")
except ImportError:
    print("Warning: matplotlib/seaborn not installed. Install with: pip install matplotlib seaborn")
    plt = None
    sns = None


class AgentEvaluator:
    """Evaluates and compares agent performance"""

    def __init__(self, data_dir: Path, output_dir: Path):
        """
        Args:
            data_dir: Directory containing evaluation game data
            output_dir: Directory for saving evaluation reports
        """
        self.data_dir = Path(data_dir)
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)

        self.games = []
        self.agent_stats = defaultdict(lambda: defaultdict(list))

    def load_games(self):
        """Load game outcome data"""
        print("Loading evaluation games...")

        outcome_files = sorted(self.data_dir.glob('game_outcomes_*.json'))
        if not outcome_files:
            print(f"No game outcome files found in {self.data_dir}")
            return

        for file in outcome_files:
            with open(file, 'r') as f:
                data = json.load(f)
                self.games.extend(data.get('outcomes', []))

        print(f"Loaded {len(self.games)} games")

    def compute_metrics(self, agent_version: str) -> Dict:
        """Compute performance metrics for an agent version"""
        print(f"\nComputing metrics for {agent_version}...")

        games_as_team = [g for g in self.games if g.get('agent_version') == agent_version]

        if not games_as_team:
            print(f"  No games found for {agent_version}")
            return {}

        metrics = {}

        # Win rate
        wins = sum(1 for g in games_as_team if g.get('outcome') == 'win')
        losses = sum(1 for g in games_as_team if g.get('outcome') == 'loss')
        draws = sum(1 for g in games_as_team if g.get('outcome') == 'draw')

        total = len(games_as_team)
        metrics['win_rate'] = wins / total if total > 0 else 0.0
        metrics['loss_rate'] = losses / total if total > 0 else 0.0
        metrics['draw_rate'] = draws / total if total > 0 else 0.0

        # Kills and deaths
        kills = [g.get('team_kills', 0) for g in games_as_team]
        deaths = [g.get('team_deaths', 0) for g in games_as_team]

        metrics['avg_kills'] = statistics.mean(kills) if kills else 0.0
        metrics['avg_deaths'] = statistics.mean(deaths) if deaths else 0.0
        metrics['kd_ratio'] = metrics['avg_kills'] / metrics['avg_deaths'] if metrics['avg_deaths'] > 0 else 0.0

        # Coordination metrics
        coordinated_kills = [g.get('coordinated_kills', 0) for g in games_as_team]
        total_kills = sum(kills)

        metrics['avg_coordinated_kills'] = statistics.mean(coordinated_kills) if coordinated_kills else 0.0
        metrics['coordination_rate'] = sum(coordinated_kills) / total_kills if total_kills > 0 else 0.0

        # MCTS efficiency (if available)
        mcts_simulations = [g.get('mcts_simulations', 0) for g in games_as_team if g.get('mcts_simulations')]
        if mcts_simulations:
            metrics['avg_mcts_simulations'] = statistics.mean(mcts_simulations)
            metrics['mcts_efficiency'] = metrics['win_rate'] / (metrics['avg_mcts_simulations'] / 1000.0)

        # Game duration
        durations = [g.get('duration_seconds', 0) for g in games_as_team if g.get('duration_seconds')]
        if durations:
            metrics['avg_game_duration'] = statistics.mean(durations)

        # Damage metrics
        damage_dealt = [g.get('team_damage_dealt', 0) for g in games_as_team if g.get('team_damage_dealt')]
        damage_taken = [g.get('team_damage_taken', 0) for g in games_as_team if g.get('team_damage_taken')]

        if damage_dealt:
            metrics['avg_damage_dealt'] = statistics.mean(damage_dealt)
        if damage_taken:
            metrics['avg_damage_taken'] = statistics.mean(damage_taken)
        if damage_dealt and damage_taken:
            metrics['damage_efficiency'] = metrics['avg_damage_dealt'] / metrics['avg_damage_taken']

        print(f"  Games: {total}")
        print(f"  Win rate: {metrics['win_rate']*100:.1f}%")
        print(f"  K/D ratio: {metrics['kd_ratio']:.2f}")
        print(f"  Coordination rate: {metrics['coordination_rate']*100:.1f}%")

        return metrics

    def compare_agents(self, baseline: str, trained: str) -> Dict:
        """Compare trained agent against baseline"""
        print("\n" + "=" * 80)
        print(f"Comparing {trained} vs {baseline}")
        print("=" * 80)

        baseline_metrics = self.compute_metrics(baseline)
        trained_metrics = self.compute_metrics(trained)

        if not baseline_metrics or not trained_metrics:
            print("Error: Missing metrics for comparison")
            return {}

        comparison = {}

        # Compute improvements
        for key in baseline_metrics.keys():
            baseline_val = baseline_metrics[key]
            trained_val = trained_metrics[key]

            if baseline_val != 0:
                improvement = ((trained_val - baseline_val) / baseline_val) * 100
            else:
                improvement = 0.0

            comparison[key] = {
                'baseline': baseline_val,
                'trained': trained_val,
                'improvement_pct': improvement
            }

        return comparison

    def generate_report(self, comparison: Dict, baseline: str, trained: str) -> str:
        """Generate evaluation report"""
        report = []
        report.append("=" * 80)
        report.append(f"Agent Evaluation Report: {trained} vs {baseline}")
        report.append("=" * 80)
        report.append(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        report.append(f"Games analyzed: {len(self.games)}")
        report.append("")

        # Key metrics
        report.append("Key Performance Metrics:")
        report.append("-" * 80)

        key_metrics = [
            ('win_rate', 'Win Rate', '%', 100),
            ('kd_ratio', 'K/D Ratio', '', 1),
            ('coordination_rate', 'Coordination Rate', '%', 100),
            ('damage_efficiency', 'Damage Efficiency', '', 1)
        ]

        for metric_key, metric_name, unit, multiplier in key_metrics:
            if metric_key in comparison:
                data = comparison[metric_key]
                baseline_val = data['baseline'] * multiplier
                trained_val = data['trained'] * multiplier
                improvement = data['improvement_pct']

                report.append(f"\n{metric_name}:")
                report.append(f"  {baseline}: {baseline_val:.2f}{unit}")
                report.append(f"  {trained}: {trained_val:.2f}{unit}")

                if improvement > 0:
                    report.append(f"  Improvement: +{improvement:.1f}% ✓")
                elif improvement < 0:
                    report.append(f"  Improvement: {improvement:.1f}% ✗")
                else:
                    report.append(f"  Improvement: 0.0%")

        report.append("")
        report.append("-" * 80)

        # MCTS efficiency (if available)
        if 'mcts_efficiency' in comparison:
            report.append("\nMCTS Efficiency:")
            data = comparison['mcts_efficiency']
            improvement = data['improvement_pct']
            report.append(f"  {trained} reaches solutions {improvement:.1f}% more efficiently")

        # Success criteria check
        report.append("\n" + "=" * 80)
        report.append("Success Criteria (from REFACTORING_PLAN.md):")
        report.append("-" * 80)

        criteria_met = 0
        criteria_total = 0

        # Criterion 1: Win rate ≥70%
        if 'win_rate' in comparison:
            criteria_total += 1
            win_rate = comparison['win_rate']['trained']
            if win_rate >= 0.70:
                report.append(f"✓ Win rate ≥70%: {win_rate*100:.1f}%")
                criteria_met += 1
            else:
                report.append(f"✗ Win rate ≥70%: {win_rate*100:.1f}% (target: 70%)")

        # Criterion 2: MCTS efficiency (50% fewer simulations)
        if 'mcts_efficiency' in comparison:
            criteria_total += 1
            improvement = comparison['mcts_efficiency']['improvement_pct']
            if improvement >= 50:
                report.append(f"✓ MCTS efficiency: +{improvement:.1f}% (target: ≥50%)")
                criteria_met += 1
            else:
                report.append(f"✗ MCTS efficiency: +{improvement:.1f}% (target: ≥50%)")

        # Criterion 3: Coordination ≥30%
        if 'coordination_rate' in comparison:
            criteria_total += 1
            coord_rate = comparison['coordination_rate']['trained']
            if coord_rate >= 0.30:
                report.append(f"✓ Coordination ≥30%: {coord_rate*100:.1f}%")
                criteria_met += 1
            else:
                report.append(f"✗ Coordination ≥30%: {coord_rate*100:.1f}% (target: 30%)")

        if criteria_total > 0:
            report.append("")
            report.append(f"Criteria met: {criteria_met}/{criteria_total}")

        report.append("=" * 80)

        return "\n".join(report)

    def plot_comparison(self, comparison: Dict, baseline: str, trained: str):
        """Generate comparison plots"""
        if plt is None:
            print("Matplotlib not available, skipping plots")
            return

        # Metrics to plot
        metrics = ['win_rate', 'kd_ratio', 'coordination_rate', 'damage_efficiency']
        metric_names = ['Win Rate', 'K/D Ratio', 'Coordination Rate', 'Damage Efficiency']

        available_metrics = [m for m in metrics if m in comparison]
        available_names = [metric_names[i] for i, m in enumerate(metrics) if m in comparison]

        if not available_metrics:
            print("No metrics available for plotting")
            return

        # Bar plot comparison
        fig, ax = plt.subplots(figsize=(10, 6))

        x = np.arange(len(available_metrics))
        width = 0.35

        baseline_values = [comparison[m]['baseline'] for m in available_metrics]
        trained_values = [comparison[m]['trained'] for m in available_metrics]

        ax.bar(x - width/2, baseline_values, width, label=baseline, color='#ff7f0e')
        ax.bar(x + width/2, trained_values, width, label=trained, color='#2ca02c')

        ax.set_xlabel('Metrics')
        ax.set_ylabel('Value')
        ax.set_title(f'Agent Performance Comparison: {trained} vs {baseline}')
        ax.set_xticks(x)
        ax.set_xticklabels(available_names, rotation=15, ha='right')
        ax.legend()
        ax.grid(axis='y', alpha=0.3)

        plt.tight_layout()
        plot_path = self.output_dir / 'comparison_plot.png'
        plt.savefig(plot_path, dpi=150)
        print(f"Plot saved to {plot_path}")
        plt.close()

        # Improvement bar plot
        fig, ax = plt.subplots(figsize=(10, 6))

        improvements = [comparison[m]['improvement_pct'] for m in available_metrics]
        colors = ['#2ca02c' if i > 0 else '#d62728' for i in improvements]

        ax.barh(available_names, improvements, color=colors)
        ax.axvline(x=0, color='black', linestyle='-', linewidth=0.8)
        ax.set_xlabel('Improvement (%)')
        ax.set_title(f'Performance Improvement: {trained} over {baseline}')
        ax.grid(axis='x', alpha=0.3)

        plt.tight_layout()
        improvement_plot_path = self.output_dir / 'improvement_plot.png'
        plt.savefig(improvement_plot_path, dpi=150)
        print(f"Improvement plot saved to {improvement_plot_path}")
        plt.close()

    def save_results(self, comparison: Dict, baseline: str, trained: str):
        """Save comparison results to JSON"""
        results = {
            'timestamp': datetime.now().isoformat(),
            'baseline': baseline,
            'trained': trained,
            'total_games': len(self.games),
            'comparison': comparison
        }

        results_path = self.output_dir / 'evaluation_results.json'
        with open(results_path, 'w') as f:
            json.dump(results, f, indent=2)

        print(f"Results saved to {results_path}")

        # Save as CSV if pandas available
        if pd is not None:
            rows = []
            for metric, data in comparison.items():
                rows.append({
                    'metric': metric,
                    'baseline': data['baseline'],
                    'trained': data['trained'],
                    'improvement_pct': data['improvement_pct']
                })

            df = pd.DataFrame(rows)
            csv_path = self.output_dir / 'evaluation_results.csv'
            df.to_csv(csv_path, index=False)
            print(f"CSV saved to {csv_path}")


def main():
    parser = argparse.ArgumentParser(
        description='Evaluate agent performance and compare against baseline',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Compare v3.0 against v2.0 baseline
  python evaluate_agents.py --data ./evaluation_data --baseline v2.0 --trained v3.0

  # Generate plots and detailed report
  python evaluate_agents.py --data ./evaluation_data --baseline v2.0 --trained v3.0 --plots
        """
    )

    parser.add_argument('--data', type=str, required=True,
                        help='Directory containing game outcome data')
    parser.add_argument('--output', type=str, default='./evaluation_results',
                        help='Output directory for evaluation reports')

    parser.add_argument('--baseline', type=str, default='v2.0',
                        help='Baseline agent version (default: v2.0)')
    parser.add_argument('--trained', type=str, default='v3.0',
                        help='Trained agent version (default: v3.0)')

    parser.add_argument('--plots', action='store_true',
                        help='Generate comparison plots (requires matplotlib)')

    args = parser.parse_args()

    # Initialize evaluator
    evaluator = AgentEvaluator(
        data_dir=Path(args.data),
        output_dir=Path(args.output)
    )

    # Load games
    evaluator.load_games()

    if not evaluator.games:
        print("\nError: No game data found!")
        print("Make sure game outcome data is exported from UE5.")
        return

    # Compare agents
    comparison = evaluator.compare_agents(args.baseline, args.trained)

    if not comparison:
        print("\nError: Could not compare agents. Check that both versions exist in data.")
        return

    # Generate report
    report = evaluator.generate_report(comparison, args.baseline, args.trained)
    print("\n" + report)

    # Save results
    evaluator.save_results(comparison, args.baseline, args.trained)

    # Save report
    report_path = evaluator.output_dir / 'evaluation_report.txt'
    with open(report_path, 'w') as f:
        f.write(report)
    print(f"\nReport saved to {report_path}")

    # Generate plots
    if args.plots:
        evaluator.plot_comparison(comparison, args.baseline, args.trained)


if __name__ == '__main__':
    main()
