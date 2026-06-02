# Stage 1 Attention Probe — Auto-generated Summary

- Run: 2026-06-02T16:18:29
- Checkpoint: `C:\Users\PC\Documents\GitHub\DE\DE_Training\training_results\20260328_032546\best`
- torch 2.5.1+cu121, numpy 2.2.6

## Global
| Metric | Value |
|---|---:|
| Padding suppression (max attn on padded slot) | 0.0 |
| Self-Cross consistency (all roles/groups) | 0.639 |

## Per-role
| Role | Pad max | Ally ΔMax (Clu/Spr) | Enemy ΔMax (Cnv/Dsp) | Cross focus shift | Self-Cross consist. | Obj pref shift (Spr-Clu) | Dup-risk Self+Cross | Dup-risk Cross-only | Self-attn gain |
|---|---|---|---|---|---|---|---|---|---|
| strike | 0.0 | 0.0945 | 0.1165 | False | 0.417 | 0.0357 | 0.0357 | 0.01789 | 0.01781 |
| vanguard | 0.0 | 0.1086 | 0.0394 | False | 0.917 | -0.00023 | -0.00023 | 0.00724 | -0.00747 |
| support | 0.0 | 0.0582 | 0.0185 | False | 0.583 | -0.0134 | -0.0134 | -0.00739 | -0.00601 |
