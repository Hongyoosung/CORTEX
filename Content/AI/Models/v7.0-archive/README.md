# v7.0 ONNX Model Archive

## Purpose
This directory contains archived v7.0 ONNX models for rollback and comparison purposes.

## v7.0 Architecture (Deprecated)
- **Action Space:** 4 discrete strategies (Assault, Defend, Support, Retreat)
- **Network:** Single-head policy [68 → 128 → 128 → 64 → 4 logits]
- **Movement:** Fixed strategy-to-position mapping (Assault→ForwardCover)
- **Combat:** Rule-based targeting (closest enemy)

## v8.0 Architecture (Current)
- **Action Space:** 4 continuous tactical params + 2 discrete combat choices
- **Network:** Multi-head policy (separate heads per strategy)
- **Movement:** RL-controlled EQS weight modulation
- **Combat:** Learned target priority (Closest vs LowestHP)

## Archived Models
When v7.0 models are trained, they should be moved here with the naming convention:
- `cortex_policy_v7.0_episodeXXXX.onnx` - Policy network checkpoints
- `cortex_policy_v7.0_final.onnx` - Final trained model

## Rollback Procedure
If v8.0 fails validation (win rate <60%):
1. Copy archived v7.0 models back to parent directory
2. Switch StateTree to use `STTask_ExecuteMovement` (v7.0)
3. Revert FollowerAgentComponent changes (use GetCurrentStrategy() instead of GetCurrentMacroAction())
4. Update Python training environment to v7.0 action space

## Version History
- **v7.0:** Single-head strategy selection (4 discrete)
- **v8.0:** Multi-head tactical parameters (4 continuous + 2 discrete combat)

---
**Archive Date:** 2026-01-14
**Deprecated By:** v8.0 Tactical Parameters Architecture
