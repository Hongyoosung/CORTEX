8 parallel environments. Each environment is staffed with 5 RL agents and 5 Script AI agents.

Each Training Environment:

- 5 RL vs 5 Script AI

- Shooting game, a capture point mode with 5 'capture points' deployed.

- The team that reaches the target score first wins. The team score is earned progressively in proportion to the number of points captured by the team.

- Individual agents respawn 10 seconds after death.

Each Agent Class:

- 'strike': Ranged DPS. High attack power and range, but low health.

- 'vanguard': Tank. Low attack power and range, but high health. Approaches enemies to perform melee attacks.

- 'support': Healer. Heals allies from the rear.

All abilities (attack, heal) are designed to automatically target enemies based on a priority policy if the target is within range.

Training script:

- "\DE_Training\training\train.py"
- "\DE_Training\training\policy.py"
- “\DE_Training\training\env_wrapper.py”