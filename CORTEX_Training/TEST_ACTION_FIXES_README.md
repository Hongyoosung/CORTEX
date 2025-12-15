# Action Execution Test Suite

Quick validation of UE5 action execution fixes.

## Quick Start

### 1. Start UE5 Environment
```bash
# Launch UE5 game with Schola server
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX\Binaries\Win64
GameAI_Project.exe -game -ResX=1280 -ResY=720 -windowed
```

Verify Schola server started:
```
[Schola] gRPC server started on port 50051
```

### 2. Run Test Script
```bash
cd C:\Users\Foryoucom\Documents\GitHub\CORTEX\CORTEX_Training

# Run all tests (recommended)
python test_action_fixes.py

# Or run specific test only
python test_action_fixes.py --test movement
python test_action_fixes.py --test crouch
python test_action_fixes.py --test fire
python test_action_fixes.py --test lookup
python test_action_fixes.py --test combined
```

### 3. Watch UE5 Output Log
**Window → Developer Tools → Output Log**

Filter for relevant logs:
- `[MOVE EXEC AI]` - Movement pathfinding
- `[CROUCH]` - Crouch state changes
- `[FIRE MASKED]` / `[FIRE ALLOWED]` - Fire action masking
- `[SCHOLA ACTUATOR]` - Action reception

---

## Expected Test Results

### ✅ Test 1: Movement (Smooth Pathfinding)

**UE5 Logs (Expected)**:
```
[MOVE EXEC AI] 'BP_FollowerAgent_C_3': NEW target (-1432.1, -1021.4, 42.3), Speed=480.0
[MOVE EXEC AI] 'BP_FollowerAgent_C_3': NEW target (-1950.3, -1120.8, 42.3), Speed=520.0
```
- **Frequency**: 1-3 logs per test (NOT 60 times/second)
- **Viewport**: Agents move smoothly without vibrating

**❌ FAILED If**:
- Logs spam every frame (60+ times/second)
- Agents vibrate in place
- No movement visible

**Fix**: Increase thresholds in `STTask_ExecuteObjective.cpp:234,243`

---

### ✅ Test 2: Crouch (State Toggling)

**UE5 Logs (Expected)**:
```
[CROUCH] 'BP_FollowerAgent_C_3': Crouching
[CROUCH] 'BP_FollowerAgent_C_3': Standing
```
- **Viewport**: Agent capsule height visibly decreases/increases

**❌ FAILED If**:
```
[CROUCH FAILED] 'BP_FollowerAgent_C_3': No CharacterMovementComponent found!
```
- **Cause**: Agent missing `UCharacterMovementComponent`
- **Fix**: Add component in Blueprint, enable `Can Crouch`

---

### ✅ Test 3: Fire (Masking When No Enemies)

**UE5 Logs (Expected - No Enemies)**:
```
[FIRE MASKED] 'BP_FollowerAgent_C_3': No targets detected (VisibleEnemyCount=0)
```

**UE5 Logs (Expected - With Enemies)**:
```
[FIRE ALLOWED] 'BP_FollowerAgent_C_6': Targets detected (VisibleEnemyCount=2)
```
- **Viewport**: No projectiles spawn when masked

**❌ FAILED If**:
- No logs appear (fire action not reaching UE5)
- Projectiles spawn when masked

**Fix**: Set `bEnableFiringMask = false` to disable masking (testing only)

---

### ✅ Test 4: Lookup (Rotation Changes)

**UE5 Logs (Expected)**:
- No specific logs (already working)

**Viewport (Expected)**:
- Agent rotates smoothly left/right/up/down
- No snapping or teleporting rotation

**❌ FAILED If**:
- Rotation doesn't change
- Rotation snaps instead of interpolating

---

### ✅ Test 5: Combined Actions

**UE5 Logs (Expected)**:
```
[MOVE EXEC AI] 'BP_FollowerAgent_C_3': NEW target (...), Speed=360.0
[CROUCH] 'BP_FollowerAgent_C_3': Crouching
[FIRE MASKED] 'BP_FollowerAgent_C_3': No targets detected (VisibleEnemyCount=0)
```

**Viewport (Expected)**:
- Agent moves forward while crouched
- Agent rotates during movement
- Fire attempted (masked/allowed)

**❌ FAILED If**:
- Only one action executes at a time
- Actions conflict (e.g., crouch cancels movement)

---

## Common Issues & Solutions

### Issue: Connection Failed
**Error**: `ConnectionRefusedError` or `DEADLINE_EXCEEDED`

**Solution**:
1. Verify UE5 is running
2. Check Output Log for: `[Schola] gRPC server started on port 50051`
3. If using Docker, verify `DefaultEngine.ini` has `Address="0.0.0.0"`

---

### Issue: Movement Still Jittering
**Symptom**: `[MOVE EXEC AI]` logs spam every frame

**Solution**:
Increase distance/direction thresholds in `STTask_ExecuteObjective.cpp`:
```cpp
if (DistToDestination < 200.0f)  // Line 234 - Was 100.0f
if (DirectionDot < 0.5f)          // Line 243 - Was 0.7f
```

---

### Issue: Crouch Not Working
**Symptom**: `[CROUCH FAILED]` error

**Solution**:
1. Open `BP_FollowerAgent` in UE5
2. Add Component → **Character Movement**
3. Set properties:
   - `Can Crouch` = **true**
   - `Crouch Height` = **48.0**
   - `Max Walk Speed` = **600.0**

---

### Issue: Fire Always Masked
**Symptom**: `[FIRE MASKED]` even when enemies visible

**Solution**:
1. Verify enemies spawned in level
2. Check perception system:
   ```cpp
   UE_LOG(LogTemp, Warning, TEXT("VisibleEnemyCount=%d"), CurrentObs.VisibleEnemyCount);
   ```
3. If always 0 → perception system issue (separate from action execution)
4. **Disable masking for testing**:
   - Set `bEnableFiringMask = false` in TacticalActuator Blueprint

---

## Success Criteria

All tests PASS if:
- ✅ Movement: Agents move smoothly (1-3 logs/test, not spam)
- ✅ Crouch: State changes logged and visible
- ✅ Fire: Masking decisions logged correctly
- ✅ Lookup: Rotation changes smoothly
- ✅ Combined: All actions execute simultaneously

If **any test fails**, check:
1. UE5 Output Log for error messages
2. Common Issues section above
3. Recompile UE5 after code changes

---

## Advanced Usage

### Custom Test Durations
Edit `test_action_fixes.py` and modify:
```python
duration=3.0  # Change to 5.0 for longer tests
```

### Test at Different Speeds
Edit action values in script:
```python
self.create_action(move_x=1.0, speed=0.8)  # Change speed to 0.5 for slower
```

### Test Specific Agents
Modify `send_actions()` to target specific agents:
```python
actions = {self.agent_ids[0]: action}  # Only first agent
```

---

## Troubleshooting Logs

### Enable Verbose Logging
Add to `DefaultEngine.ini`:
```ini
[Core.Log]
LogTemp=Verbose
```

### Key Log Patterns

**Movement Working**:
```
[MOVE EXEC AI] Agent: NEW target (...), Speed=480.0
(~1 second pause - agent moving)
[MOVE EXEC AI] Agent: NEW target (...), Speed=520.0
```

**Movement Broken**:
```
[MOVE EXEC AI] Agent: NEW target (-1432.1, -1021.4, 42.3)
[MOVE EXEC AI] Agent: NEW target (-1435.2, -1022.1, 42.3)  ← 0.01s later!
[MOVE EXEC AI] Agent: NEW target (-1438.4, -1023.5, 42.3)  ← Spamming!
```

**Crouch Working**:
```
[CROUCH] Agent: Crouching
(2 seconds later)
[CROUCH] Agent: Standing
```

**Crouch Broken**:
```
[CROUCH FAILED] Agent: No CharacterMovementComponent found!
```

---

## Next Steps After Testing

### If All Tests Pass ✅
1. Re-enable fire masking (if disabled for testing)
2. Start training: `python train_rllib.py`
3. Monitor TensorBoard for:
   - `episode_reward_mean` increasing
   - `episode_len_mean` stabilizing
   - Coordination metrics improving

### If Tests Fail ❌
1. Identify which test(s) failed
2. Check "Common Issues & Solutions" section
3. Review UE5 Output Log for error messages
4. Recompile UE5 after any C++ changes
5. Re-run tests to verify fixes

---

## Contact & Support

If tests continue to fail after following troubleshooting steps, provide:
1. UE5 Output Log (filter for `[MOVE`, `[CROUCH`, `[FIRE`)
2. Test script output
3. Screenshot of UE5 viewport during test
4. Which specific test(s) failed
