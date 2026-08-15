# 2026-08-15 Flight Deck measurement data

This directory contains the raw reports used by
[`PERFORMANCE_ENVELOPE_2026-08-15.md`](../../PERFORMANCE_ENVELOPE_2026-08-15.md).

- `direct-*.json`: 60-second live captures in local Kitty 0.48.2.
- `direct-limit-*.json`: selected 15-second custom-viewport captures around the
  exploratory direct-path 30 FPS crossover.
- `rdp-*.json`: 60-second live captures in Kitty 0.32.2 over RDP.
- `headless-gcc.json` and `headless-clang.json`: 180-frame deterministic
  sweeps for renderer and frame-generation baselines.
- `direct-environment.txt` and `rdp-environment.txt`: terminal and toolchain
  records captured with the live runs.

All reports use seed `12648430` and source commit
`07bf95cb4b2f07bcd90f03d2f85cce19cd6a6daf`.
