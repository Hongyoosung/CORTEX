```
Objective Capture System Refactoring
Overview
The existing Objective system is being converted to a durability-based capture system. This introduces new gameplay mechanics while maintaining full compatibility with the existing architecture.

Objective Actor Component Structure
Required Components:

Static Mesh Component: Central pillar mesh

Team-specific material instances displaying the team's color

Display a debug indicator on the Object Actor to display durability values

Capture Volume Component: A spherical or box-shaped trigger volume

Visualized with a translucent green material (Editor and Debug Mode)

Bind the OnComponentBeginOverlap and OnComponentEndOverlap events

Maintain a TArray or TSet to track agents within the volume

Durability System Implementation
Core Properties:

float CurrentDurability: Current durability (0.0 to 100.0)

float MaxDurability: Maximum durability (default: 100.0)

int32 OwnerTeamID: Team ID

TSet<AActor*> HostileAgentsInVolume: Set of hostile agents within the volume

Durability Mechanics:

Decline Logic: Tick or Timer-based (recommended: (Using FTimerHandle)

Reduction per second = HostileAgentsInVolume.Num() * 2.0f

Only executes when there is at least 1 enemy agent.

Recovery logic:

1.0% recovery per second when HostileAgentsInVolume.IsEmpty() == true.

Recovery only executes when CurrentDurability < MaxDurability.

Defeat conditions:

The team loses when CurrentDurability <= 0.0f.

The episode is reset after broadcasting the defeat event in the game mode.

System Compatibility Guidelines
Deprecated Features - Items to be removed:

Existing 'Defend'/'Capture' type distinction enum and related logic

Type-specific flags or properties for each Objective

Type-based branching code

Team-Objective Association:

Each team recognizes the Objective whose OwnerTeamID matches its own team ID as a friendly base.

Example logic:

cpp
if (Objective->OwnerTeamID == Agent->TeamID) {
// Defend objective
} else {
// Capture objective (enemy base)
}
Assign the corresponding objective reference to each team upon initialization in game mode or via the Team Manager.

Reward System Redesign
Completely removes the existing distance-based reward calculation logic.

New reward system:

Assault Role:

Capture Volume Retention Reward: Continuous reward while inside the enemy Objective's Capture Volume.

Recommended: Small cumulative reward per second (e.g., +0.1/sec)

Base Destruction Reward: Large reward for reducing the enemy Objective's durability to 0% and winning.

Recommended: +50.0 to +100.0 (requires scaling)

Defense Role:

Defense Volume Retention Reward: Continuous reward while inside the friendly Objective's Capture Volume.

Recommended: Small cumulative reward per second (e.g., +0.05/sec)

Defense Kill Reward: Additional reward for killing an enemy within an ally's Capture Volume.

Base kill reward +2.0 ~ +5.0 bonus

Verify if the victim's location is within the friendly volume when taking a kill.

Implementation Checklist
Add new properties and components to the Objective class.

Implement an Overlap event handler for the Capture Volume.

Implement durability reduction/recovery timer logic.

Check for defeat conditions and integrate with game modes.

Remove the existing Defend/Capture type system.

Implement team-to-objective mapping logic.

Completely replace the reward system (from distance-based to volume-based).

Implement a material and visual feedback system.

Unit Test: Single/Multiple Agent Scenarios

Integration Test: Simulate gameplay across two teams.

Notes
Performance Optimization: HostileAgentsInVolume updates are only performed during the Overlap event.

If network synchronization is required, set CurrentDurability to the Replicated property.

Volume visualization is enabled only in development builds (disabled in shipping builds).
```