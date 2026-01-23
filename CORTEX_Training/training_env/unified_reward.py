"""
v8.0 Unified Reward System with Strategy-Specific Weights

Key Innovation: Single reward calculator with strategy-specific weight profiles.
All strategies use the same reward components but with different weights,
ensuring maintainability and debuggability.

Architecture Benefits:
- Single source of truth for reward computation
- Easy hyperparameter tuning via weight profiles
- TensorBoard visualization of component contributions
- No code duplication across strategies
"""

from enum import IntEnum
from typing import Dict, Tuple, Optional
import numpy as np


class EStrategyType(IntEnum):
    """Strategy types (assigned by MCTS in v8.0)"""
    Assault = 0
    Defend = 1
    Support = 2
    Retreat = 3


class ETargetPriority(IntEnum):
    """Target priority options (RL-learned in v8.0)"""
    Closest = 0
    LowestHP = 1


# ============================================================================
# STRATEGY-SPECIFIC WEIGHT PROFILES
# Single source of truth for reward tuning
# ============================================================================

REWARD_WEIGHTS = {
    'Assault': {
        'objective_progress':     1.0,   # High: push toward objective
        'combat_effectiveness':   0.8,   # High: engage enemies
        'survival':              0.6,   # Medium: acceptable risk
        'cover_usage':           0.3,   # Low: use cover but don't camp
        'team_coordination':     0.4,   # Medium: loose formation
    },

    'Defend': {
        'objective_progress':     0.2,   # Low: stay near objective
        'combat_effectiveness':   0.6,   # Medium: suppress threats
        'survival':              1.0,   # High: must survive to hold position
        'cover_usage':           0.9,   # High: maximize cover usage
        'team_coordination':     0.5,   # Medium: defensive formation
    },

    'Support': {
        'objective_progress':     0.5,   # Medium: follow ally's objective
        'combat_effectiveness':   0.4,   # Medium-low: assist, don't solo
        'survival':              0.7,   # Medium-high: stay alive to support
        'cover_usage':           0.5,   # Medium: balanced positioning
        'team_coordination':     1.0,   # High: stick with ally
    },

    'Retreat': {
        'objective_progress':     0.8,   # High: reach safe zone
        'combat_effectiveness':   0.0,   # None: avoid combat entirely
        'survival':              1.2,   # Highest: survival paramount
        'cover_usage':           0.7,   # High: use cover while retreating
        'team_coordination':     0.3,   # Low: self-preservation priority
    },
}


# ============================================================================
# AGENT STATE
# Encapsulates all information needed for reward calculation
# ============================================================================

class AgentState:
    """State snapshot for reward calculation"""

    def __init__(self):
        # Position & movement
        self.position: np.ndarray = np.zeros(3)
        self.velocity: np.ndarray = np.zeros(3)

        # Health & survival
        self.health: float = 1.0
        self.died: bool = False

        # Objective tracking
        self.distance_to_objective: float = 0.0
        self.last_distance_to_objective: float = 0.0
        self.in_objective_volume: bool = False

        # Combat events
        self.dealt_damage: bool = False
        self.damage_amount: float = 0.0
        self.killed_enemy: bool = False
        self.target_was_lowest_hp: bool = False

        # Tactical positioning
        self.in_cover: bool = False

        # Team coordination
        self.ally_state: Optional['AgentState'] = None
        self.distance_to_ally: float = 0.0


# ============================================================================
# UNIFIED REWARD CALCULATOR
# Single implementation, strategy-specific behavior via weights
# ============================================================================

class StrategyRewardCalculator:
    """
    Unified reward calculator for all strategies.

    Key Design:
    - All strategies use same components (objective, combat, survival, cover, coordination)
    - Strategy-specific behavior emerges from weight profiles
    - Easy to debug via TensorBoard component breakdown
    - Easy to tune via weight profile adjustments
    """

    def __init__(self, strategy_type: EStrategyType):
        """
        Args:
            strategy_type: Which strategy this calculator is for
        """
        strategy_name = EStrategyType(strategy_type).name
        self.weights = REWARD_WEIGHTS[strategy_name]
        self.strategy_type = strategy_type

        # For TensorBoard logging
        self.last_component_values: Dict[str, float] = {}

    def calculate(self, state: AgentState) -> Tuple[float, Dict[str, float]]:
        """
        Compute total reward as weighted sum of reward components.

        Args:
            state: Current agent state

        Returns:
            (total_reward, component_breakdown) for TensorBoard logging
        """
        components = {}

        # Component 1: Objective Progress
        obj_reward = self._objective_reward(state)
        components['objective_progress'] = obj_reward * self.weights['objective_progress']

        # Component 2: Combat Effectiveness
        combat_reward = self._combat_reward(state)
        components['combat_effectiveness'] = combat_reward * self.weights['combat_effectiveness']

        # Component 3: Survival
        survival_reward = self._survival_reward(state)
        components['survival'] = survival_reward * self.weights['survival']

        # Component 4: Cover Usage
        cover_reward = self._cover_reward(state)
        components['cover_usage'] = cover_reward * self.weights['cover_usage']

        # Component 5: Team Coordination
        coord_reward = self._coordination_reward(state)
        components['team_coordination'] = coord_reward * self.weights['team_coordination']

        # Total reward
        total_reward = sum(components.values())

        # Cache for TensorBoard
        self.last_component_values = components

        return total_reward, components

    # ========================================================================
    # REWARD COMPONENT IMPLEMENTATIONS (Strategy-Agnostic)
    # These are the same for all strategies, weights determine behavior
    # ========================================================================

    def _objective_reward(self, state: AgentState) -> float:
        """Progress toward assigned objective (strategy determines weight)"""
        reward = 0.0

        # Moving closer to objective
        if state.distance_to_objective < state.last_distance_to_objective:
            reward += 0.1  # +0.1 per step when advancing

        # Inside objective volume (continuous retention)
        if state.in_objective_volume:
            reward += 0.1  # +0.1 per step while in volume (~1.0/sec at 10Hz)

        return reward

    def _combat_reward(self, state: AgentState) -> float:
        """Combat effectiveness (kills, damage dealt)"""
        reward = 0.0

        # Damage dealt
        if state.dealt_damage:
            reward += state.damage_amount / 10.0  # +0.1 per 1 damage

        # Kill rewards (v8.0: includes target priority bonus)
        if state.killed_enemy:
            if state.target_was_lowest_hp:
                reward += 12.0  # Bonus for efficient target selection
            else:
                reward += 10.0  # Base kill reward

        return reward

    def _survival_reward(self, state: AgentState) -> float:
        """Survival/death penalty"""
        if state.died:
            return -10.0  # Base death penalty (scaled by weight)
        return 0.0

    def _cover_reward(self, state: AgentState) -> float:
        """Cover usage and tactical positioning"""
        if state.in_cover:
            return 0.1  # Continuous cover bonus
        return 0.0

    def _coordination_reward(self, state: AgentState) -> float:
        """Team coordination (proximity to allies, formation)"""
        reward = 0.0

        if state.ally_state:
            distance = state.distance_to_ally
            # Optimal support range: 200cm to 800cm (2m to 8m)
            if 200 < distance < 800:
                reward += 0.1  # Formation bonus

        return reward

    def get_last_breakdown(self) -> Dict[str, float]:
        """Get last reward component breakdown for TensorBoard logging"""
        return self.last_component_values


# ============================================================================
# REWARD CALCULATOR FACTORY
# Creates appropriate calculator based on strategy
# ============================================================================

def create_reward_calculator(strategy_type: EStrategyType) -> StrategyRewardCalculator:
    """
    Factory method to create strategy-specific reward calculator.

    Args:
        strategy_type: Strategy to create calculator for

    Returns:
        Configured StrategyRewardCalculator instance
    """
    return StrategyRewardCalculator(strategy_type)


# ============================================================================
# TENSORBOARD LOGGING HELPERS
# For reward component visualization during training
# ============================================================================

def log_reward_breakdown_to_tensorboard(
    writer,
    global_step: int,
    agent_id: str,
    strategy_type: EStrategyType,
    components: Dict[str, float],
    total_reward: float
):
    """
    Log reward component breakdown to TensorBoard.

    Args:
        writer: TensorBoard SummaryWriter
        global_step: Training step number
        agent_id: Agent identifier
        strategy_type: Current strategy
        components: Component breakdown from calculator
        total_reward: Total reward value
    """
    strategy_name = EStrategyType(strategy_type).name

    # Log total reward
    writer.add_scalar(f'Rewards/{agent_id}/total', total_reward, global_step)
    writer.add_scalar(f'Rewards/{strategy_name}/total', total_reward, global_step)

    # Log individual components
    for component_name, value in components.items():
        writer.add_scalar(
            f'Rewards/{agent_id}/components/{component_name}',
            value,
            global_step
        )
        writer.add_scalar(
            f'Rewards/{strategy_name}/components/{component_name}',
            value,
            global_step
        )

    # Log component ratios (helps identify which components dominate)
    if total_reward != 0:
        for component_name, value in components.items():
            ratio = value / total_reward
            writer.add_scalar(
                f'Rewards/{strategy_name}/ratios/{component_name}',
                ratio,
                global_step
            )


# ============================================================================
# EXAMPLE USAGE
# ============================================================================

if __name__ == "__main__":
    # Example: Create calculator for Assault strategy
    assault_calc = create_reward_calculator(EStrategyType.Assault)

    # Example state
    state = AgentState()
    state.distance_to_objective = 450.0
    state.last_distance_to_objective = 500.0  # Moving closer
    state.in_objective_volume = False
    state.dealt_damage = True
    state.damage_amount = 25.0
    state.killed_enemy = True
    state.target_was_lowest_hp = True
    state.in_cover = False
    state.died = False

    # Calculate reward
    total_reward, components = assault_calc.calculate(state)

    print(f"Strategy: Assault")
    print(f"Total Reward: {total_reward:.2f}")
    print(f"Component Breakdown:")
    for name, value in components.items():
        print(f"  {name}: {value:.2f}")

    # Expected output:
    # - Objective progress: 0.1 * 1.0 = 0.1 (high weight for Assault)
    # - Combat effectiveness: (2.5 + 12.0) * 0.8 = 11.6 (high weight)
    # - Survival: 0.0 * 0.6 = 0.0 (no death)
    # - Cover: 0.0 * 0.3 = 0.0 (not in cover, low weight anyway)
    # - Coordination: 0.0 * 0.4 = 0.0 (no ally)
    # Total: ~11.7

    print("\n" + "="*60)

    # Compare with Defend strategy (same state)
    defend_calc = create_reward_calculator(EStrategyType.Defend)
    total_reward_defend, components_defend = defend_calc.calculate(state)

    print(f"Strategy: Defend")
    print(f"Total Reward: {total_reward_defend:.2f}")
    print(f"Component Breakdown:")
    for name, value in components_defend.items():
        print(f"  {name}: {value:.2f}")

    # Expected output:
    # - Objective progress: 0.1 * 0.2 = 0.02 (low weight for Defend)
    # - Combat effectiveness: 14.5 * 0.6 = 8.7 (medium weight)
    # - Survival: 0.0 * 1.0 = 0.0 (high weight but no death)
    # - Cover: 0.0 * 0.9 = 0.0 (high weight but not in cover)
    # - Coordination: 0.0 * 0.5 = 0.0 (no ally)
    # Total: ~8.72 (LOWER than Assault due to different weights)

    print("\nNotice: Same events, different total rewards due to weight profiles!")
