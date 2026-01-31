# Resume Training from Checkpoint

## Quick Reference

### Docker Mode (Recommended)
```bash
cd CORTEX_Training
docker_train.bat
# Select option 3: Resume from checkpoint
# Enter checkpoint directory name (e.g., 20260131_100243)
# Enter number of additional iterations
```

### Python Direct Mode
```bash
cd CORTEX_Training

# List available checkpoints
dir training_results

# Resume from specific checkpoint
python train_rllib.py --resume "training_results/20260131_100243" --iterations 50

# Resume with custom host/port
python train_rllib.py --resume "training_results/20260131_100243" --iterations 50 --host localhost --port 50051
```

## How It Works

1. **Checkpoint Detection**: Automatically detects the last completed iteration from `progress.csv`
2. **State Restoration**: Loads algorithm state, policy weights, and optimizer state
3. **Statistics Preservation**: Continues tracking cumulative episodes, steps, and best reward
4. **Seamless Continuation**: Training continues from where it left off

## What Gets Restored

- ✅ Policy network weights
- ✅ Optimizer state (Adam momentum, etc.)
- ✅ Training iteration counter
- ✅ Cumulative episode count
- ✅ Cumulative step count
- ✅ Best reward achieved
- ✅ Learning rate schedule position

## Example Workflow

### Scenario: Training Interrupted at Iteration 19

```bash
# Original training (interrupted)
python train_rllib.py --iterations 100
# ... training runs until iteration 19, then crashes/interrupted

# Resume training (completes remaining 81 iterations)
python train_rllib.py --resume "training_results/20260131_100243" --iterations 81

# Or train even longer (add 100 more iterations on top of 19)
python train_rllib.py --resume "training_results/20260131_100243" --iterations 100
# Will train iterations 20-119
```

## Checkpoint Structure

```
training_results/
└── 20260131_100243/
    ├── rllib_checkpoint.json     # Checkpoint metadata
    ├── algorithm_state.pkl        # Algorithm state
    ├── params.json                # Configuration
    ├── params.pkl                 # Configuration (pickle)
    ├── policies/                  # Policy weights
    │   └── shared_policy/
    ├── progress.csv               # Training history
    ├── result.json                # Results log
    └── best/                      # Best model checkpoint
        ├── rllib_checkpoint.json
        ├── algorithm_state.pkl
        └── policies/
```

## Troubleshooting

### Error: "Checkpoint directory not found"
- Check the path is correct: `training_results/YYYYMMDD_HHMMSS`
- Use absolute path if relative path fails

### Error: "Could not read iteration from progress.csv"
- Checkpoint may be corrupted
- Training will resume from iteration 0 (but with restored weights)

### Error: "Failed to restore checkpoint"
- Ensure RLlib version matches (check `rllib_checkpoint.json`)
- Verify all checkpoint files are present
- Try restoring from `best/` subdirectory if main checkpoint is corrupted

### Docker Volume Mount Issues
- Ensure checkpoint directory is within `CORTEX_Training/training_results/`
- Check Docker has file sharing permissions for the directory

## Advanced Usage

### Resume from Best Model
```bash
python train_rllib.py --resume "training_results/20260131_100243/best" --iterations 50
```

### Override Host/Port on Resume
```bash
python train_rllib.py --resume "training_results/20260131_100243" --iterations 50 --host 192.168.1.100 --port 50052
```

### Resume with Different Checkpoint Frequency
```bash
python train_rllib.py --resume "training_results/20260131_100243" --iterations 50 --checkpoint-freq 5
```

## Notes

- Resuming does NOT overwrite the original checkpoint directory
- All new progress is saved to the same directory
- If training is interrupted again, you can resume from the same checkpoint
- The iteration counter continues from where it left off (e.g., 20, 21, 22...)
