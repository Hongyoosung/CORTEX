# v10.2 Quick Start Guide

**TL;DR:** Your v10.2 actuator and Python training script are ready. This is your cheat sheet.

---

## 🎯 What Changed (v8.0 → v10.2)

```
❌ OLD (v8.0): 4-dim [0,1] → Abstract Parameters → Manual EQS mapping
✅ NEW (v10.2): 7-dim [-1,1] → Direct EQS Weights → Automatic application
```

---

## 📦 What You Got

### 1. C++ Actuator (Training-Time)
```
✅ Public/Schola/Actuators/TacticalParameterActuator_v10_2.h
✅ Private/Schola/Actuators/TacticalParameterActuator_v10_2.cpp
```

**Action Space:** `Box([-1, 1]^7)`

**7 Dimensions:**
1. EnemyObjectiveProximity
2. AllyObjectiveProximity
3. CoverDensity
4. EnemyVisibility
5. AllyProximity
6. CombatRange
7. PickupProximity

### 2. Python Training Script
```
✅ MOC_Training/training/phase1_policy_training_v10_2.py
```

**Key Classes:**
- `MultiHeadRLPolicy_v10_2` - 3 strategy heads (Assault/Defend/Support)
- `PPOTrainer_v10_2` - PPO training loop
- `StrategyBalancedReplayBuffer` - Balanced sampling

### 3. Documentation
```
✅ ACTUATOR_v10.2_SUMMARY.md - Actuator architecture & integration
✅ PYTHON_v10.2_INTEGRATION.md - Python training guide
✅ QUICK_START_v10.2.md - This file
```

---

## 🚀 Minimal Python Example

```python
import torch
from phase1_policy_training_v10_2 import MultiHeadRLPolicy_v10_2

# 1. Initialize policy
policy = MultiHeadRLPolicy_v10_2(
    obs_dim=52,           # Local observation
    num_strategies=3,     # Assault/Defend/Support
    eqs_dim=7            # 7-dim EQS weights
)

# 2. Inference (single step)
obs = torch.randn(1, 52)                   # Your observation
strategy = torch.tensor([0])               # 0=Assault, 1=Defend, 2=Support

with torch.no_grad():
    eqs_weights = policy(obs, strategy)    # Output: (1, 7) in [-1, 1]
    action = eqs_weights.numpy()[0]        # Convert to numpy

# 3. Send to UE5
env.step(action=action)  # Actuator receives and applies
```

---

## 🔌 Blueprint Integration (Your Side)

### Step 1: Add Actuator Component
```
Blueprint: BP_Agent or BP_MocCharacter
Components:
  + ScholaMocAgent (already exists)
  + TacticalParameterActuator_v10_2 ← ADD THIS

Settings:
  - bAutoFindMoc = true
  - bDebugLogging = false (enable for testing)
  - bClampOutputs = true
```

### Step 2: Verify Action Space
```cpp
// In ScholaMocAgent or Schola plugin
FBoxSpace ActionSpace = Actuator->GetActionSpace();
check(ActionSpace.Dimensions.Num() == 7);
check(ActionSpace.Dimensions[0].Min == -1.0f);
check(ActionSpace.Dimensions[0].Max == 1.0f);
```

### Step 3: Test
```
1. Start UE5 Training Map
2. Enable bDebugLogging on actuator
3. Run Python training script
4. Watch Output Log for:
   [TacticalParameterActuator_v10_2] Agent=BP_Agent_0, Strategy=0, Action=1:
   E_Obj:0.80, A_Obj:-0.30, Cover:0.60, Vis:0.40, Ally:0.20, ...
```

---

## 📝 Action Validation Checklist

Before sending actions to UE5:

```python
# ✓ Shape check
assert action.shape == (7,), f"Expected (7,), got {action.shape}"

# ✓ Range check
assert (action >= -1.0).all(), f"Min value: {action.min()}"
assert (action <= 1.0).all(), f"Max value: {action.max()}"

# ✓ Finite check
assert np.isfinite(action).all(), "Contains NaN or Inf"

# ✓ Data type
assert action.dtype == np.float32, f"Expected float32, got {action.dtype}"
```

---

## 🐛 Common Issues

### Issue: "Action dimension mismatch"
```python
# ❌ Wrong
action = policy(obs, strategy).numpy()  # Shape: (1, 7)
env.step(action)

# ✅ Correct
action = policy(obs, strategy).numpy()[0]  # Shape: (7,)
env.step(action)
```

### Issue: "Weights out of range"
```python
# Check policy head has tanh activation
print(policy.assault_head[-1])  # Should print: Tanh()

# If missing, your policy is wrong - use phase1_policy_training_v10_2.py
```

### Issue: "Actuator not receiving actions"
```
1. Check Blueprint: ScholaMocAgent has TacticalParameterActuator_v10_2 component
2. Check C++: Actuator is registered with Schola
3. Check Python: env.step(action) is called correctly
4. Enable bDebugLogging to see if TakeAction() is called
```

---

## 🎓 Training Workflow

```
┌─────────────────────────────────────────────────────────┐
│ 1. Setup Policy                                         │
│    policy = MultiHeadRLPolicy_v10_2(...)                │
│    trainer = PPOTrainer_v10_2(policy)                   │
└─────────────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────────────┐
│ 2. Connect to UE5                                       │
│    env = MocEnvironment_v10_2(host='localhost')         │
└─────────────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────────────┐
│ 3. Training Loop                                        │
│    for episode in range(num_episodes):                  │
│        obs = env.reset()                                │
│        while not done:                                  │
│            action = policy(obs['observation'],          │
│                           obs['commanded_strategy'])    │
│            next_obs, reward, done = env.step(action)    │
│            replay_buffer.add(transition)                │
│        if len(replay_buffer) > batch_size:              │
│            batch = replay_buffer.sample(batch_size)     │
│            trainer.update(batch)                        │
└─────────────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────────────┐
│ 4. Export ONNX                                          │
│    policy.export_onnx('policy_v10_2.onnx')              │
└─────────────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────────────┐
│ 5. Load in UE5 (Runtime Inference)                      │
│    UMocPolicyExecutor::LoadModel("policy_v10_2.onnx")   │
└─────────────────────────────────────────────────────────┘
```

---

## 📊 Expected Outputs

### Policy Output (Python)
```python
array([ 0.82, -0.31,  0.64,  0.39,  0.18, -0.05, -0.52], dtype=float32)
       │      │       │      │      │      │       │       
       │      │       │      │      │      │       │       
       │      │       │      │      │      │       └─ PickupProximity: -0.52 (ignore pickups)
       │      │       │      │      │      └─ CombatRange: -0.05 (neutral)
       │      │       │      │      └─ AllyProximity: +0.18 (loose formation)
       │      │       │      └─ EnemyVisibility: +0.39 (moderate exposure)
       │      │       └─ CoverDensity: +0.64 (prefer cover)
       │      └─ AllyObjectiveProximity: -0.31 (don't hug base)
       └─ EnemyObjectiveProximity: +0.82 (approach enemy)
```

### UE5 Log (Debug Mode)
```
LogTemp: [TacticalParameterActuator_v10_2] Initialized for BP_Agent_C_0
LogTemp: [TacticalParameterActuator_v10_2] Agent=BP_Agent_C_0, Strategy=0, Action=1:
         E_Obj:0.82, A_Obj:-0.31, Cover:0.64, Vis:0.39, Ally:0.18, Rng:-0.05, Pick:-0.52, H_Adv:0.71
```

---

## 🔗 Key Files Reference

### C++ (UE5)
```
Actuator:
  TacticalParameterActuator_v10_2.h/.cpp

Types:
  Public/Types/EQSTypes.h (FEQSWeightParameters)
  Public/Types/StrategyTypes.h (EStrategyType)

Integration:
  Public/AI/AIController/MocAIController.h (UpdateBlackboardWeights)
  Public/AI/Policy/MocPolicyExecutor.h (InferWeights)
```

### Python (Training)
```
Training:
  MOC_Training/training/phase1_policy_training_v10_2.py

Docs:
  MOC_Training/PYTHON_v10.2_INTEGRATION.md
  Source/GameAI_Project/ACTUATOR_v10.2_SUMMARY.md
```

---

## ✅ Final Checklist

Before starting training:

- [ ] Actuator files compiled in UE5 (no errors)
- [ ] Blueprint has TacticalParameterActuator_v10_2 component
- [ ] Python script imports successfully
- [ ] Policy initializes without errors
- [ ] Action space is Box([-1, 1]^7)
- [ ] Observation includes `commanded_strategy`
- [ ] Test inference produces valid actions
- [ ] Debug logging enabled for initial test
- [ ] Schola environment returns 52-dim obs

---

## 🎯 Success Criteria

After 1000 training steps, you should see:

- **Action Range**: All weights in [-1, 1] ✓
- **Strategy Distribution**: ~33% each (Assault/Defend/Support) ✓
- **Loss Convergence**: Policy loss decreasing ✓
- **Entropy**: Gradually decreasing (exploration → exploitation) ✓
- **Reward**: Positive trend ✓
- **No Errors**: No NaN, no crashes ✓

---

## 📚 Learn More

- **Full Architecture**: `v10.2Architecture.md`
- **Python Integration**: `PYTHON_v10.2_INTEGRATION.md`
- **Actuator Details**: `ACTUATOR_v10.2_SUMMARY.md`
- **Project Overview**: `CLAUDE.md`

---

**Questions?** Check the integration docs above or enable debug logging.

**Ready?** Run `python phase1_policy_training_v10_2.py` and watch the magic! ✨
