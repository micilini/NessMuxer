# N.148 Benchmark Matrix

This document establishes the workflow for **continuous benchmark and tuning guided by data**.

## Goal

Every meaningful compression change should prove itself with:

- final file size
- elapsed encode time
- guardrail status against the current 243 KB target for the known `video_nv12.raw` scenario
- reproducible CSV output for later historical comparison

## Current command

```bash
./build/n148_regression_bench \
  video_nv12.raw \
  --width 800 \
  --height 600 \
  --fps 24 \
  --bitrate 2278 \
  --iterations 3 \
  --csv n148_regression.csv \
  --label n148_cabac
```

## CSV columns

- `label`
- `iteration`
- `bytes`
- `elapsed_ms`
- `frames`
- `avg_kbps`
- `guardrail_243kb_pass`

## Interpretation

- **bytes** is the most important acceptance metric for this phase.
- **elapsed_ms** keeps performance regressions visible.
- **guardrail_243kb_pass** makes the current target explicit and machine-readable.

## Suggested workflow

1. Freeze a baseline run before touching compression heuristics.
2. Apply one meaningful tuning change at a time.
3. Re-run the same benchmark command.
4. Keep the change only if it improves size, time, or both.
5. Store CSV snapshots per relevant change.

## Note about quality metrics

The repo already has PSNR/SSIM primitives in `src/common/n148_metrics.c`, but this phase focuses first on a stable regression harness for size/time guardrails.
The next step is wiring a complete encoded-output decode loop into the benchmark path so size, speed, and quality can be tracked together for every clip.
