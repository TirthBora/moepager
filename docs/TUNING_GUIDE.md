# Daemon Configuration and Tuning Guide

How to run and tune `moepagerd`. Every flag below exists (`moepagerd
--help`). Every recommendation is either measured (Phase 7, OLMoE-1B-7B on
a 15 GB laptop, BENCHMARKS.md) or explicitly marked as untested.

---

## Recommended setup (measured)

```bash
# 1. Engine: weights in the page cache, fault read-around off.
LD_PRELOAD=$PWD/tools/preload/libmoepager_preload.so \
  llama-cli -m model.gguf -nr ...
# 2. Daemon: expert-completion readahead, no pinning, no privileges.
target/release/moepagerd model.gguf --ops linux --source sentinel --policy v --budget-mb 0
```

With both inside the same cgroup and a 1.5 GB / 2.5 GB `memory.max`, this
decoded 1.40 / 3.21 tok/s, against 0.35 / 0.87 tok/s for `-nr` alone.

- **Do not run the daemon without the shim** (read-around on). It then
  helped only 1.3–1.4×. With btrfs's 4 MiB read-around, its sentinels fire
  on pages pulled in as collateral.
- Pass `--pid <engine pid>`. The daemon then warns at start-up if the
  engine's mapping still has read-around on (missing `rr` in
  `/proc/<pid>/smaps` VmFlags).

## Flags

| Flag | Default | Meaning / guidance |
|---|---|---|
| `--policy` | `v` | `v` (completion readahead, plus V(e) pinning if `--budget-mb` > 0), `lru-pin`, `willneed-all` (naive baseline), `none` (observe only) |
| `--budget-mb` | `0` | Pin budget. **0 recommended.** Pinning needs `CAP_IPC_LOCK` or a raised `RLIMIT_MEMLOCK` (8 MB default), and V(e) pinning underperformed LFU in simulation. *Untested end to end.* |
| `--source` | `sentinel` | `sentinel` (unprivileged `mincore` polling) or `trace:<file.mpt>` (dry run) |
| `--ops` | `mock` | Use `linux` for real effect; `mock` only logs |
| `--interval-us` | `500` | Sentinel poll period. Phase 7 used `300` |
| `--no-completion` | off | Disables expert-completion readahead, the main mechanism. Keep it enabled |
| `--refractory-ms` | `0` | Ignore repeated misses of a unit for this long. **Keep 0**: 20 ms doubled misses and cut throughput from 5.4 to 2.1 tok/s in the Phase 7 ablation |
| `--prefetch-mode` | `willneed` | `willneed` = chunked `fadvise` (asynchronous; measured). `populate` = `MADV_POPULATE_READ` on a worker thread, for devices where WILLNEED has no effect (readahead disabled). Populate is tested on a real file but not benchmarked end to end |
| `--predict` | off | Cross-layer predictive prefetch. Not recommended: wasted bandwidth in simulation, and its deadline estimate includes I/O stalls |
| `--pid` | none | Engine pid. Enables two start-up checks (read-around still on; daemon in a different cgroup from the engine) and `MADV_COLD` demotion (needs `CAP_SYS_NICE`, untested) |
| `--duration-s` | none | Stop after N seconds (sentinel source) |
| `--stats-json` | none | Write counters on exit (SIGINT/SIGTERM exit cleanly) |

Hysteresis (1.25), re-plan cadence (4 tokens), exploration fraction (0.02)
and the decay half-life (64 tokens) are `ResidencyConfig` / `DaemonConfig`
fields in code. They have **no CLI flags** and only matter when pinning is
enabled.

## Memory budget

- Run the engine and daemon in one cgroup, so pages the daemon prefetches
  count against the same limit:
  `systemd-run --user --scope -p MemoryMax=<C> -p MemorySwapMax=0 -- …`
  (see `bench/cgroup_run.sh`).
- Default llama.cpp (with repacking) cannot run in such a budget without
  swap. It was OOM-killed at 1.5 GB and 2.5 GB.

## Untested

- Larger models (Qwen3-30B-A3B, gpt-oss-20b), ext4 (128 KiB read-around),
  disk swap, cloud VMs: no measurements yet.
- Pinning profiles and any "optimal" budget values: not measured.
