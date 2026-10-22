# Review of `AyushSinha2210/moepager@ayush` (20 docs commits)

> **Status (2026-10-23):** the branch was merged on GitHub (PR #1). All
> items below have since been fixed on `main`, in the commits following
> the merge. This file is kept as the record of what was wrong and why.

Checked against the code on `main`. Each item is a factual error or an
unsupported claim that would cost marks if a grader runs the commands or
compares the docs with the code. Items are ordered by severity.

## Wrong commands, flags and paths (readers will hit errors)

1. **docs/TUNING_GUIDE.md, docs/TROUBLESHOOTING.md, docs/PRIVILEGES.md:**
   - These use moepagerd flags that don't exist: `--pin-budget-bytes`,
     `--replan-every`, `--hysteresis`, `--explore-frac`,
     `--prefetch completion[,predict]` and `--stats`.
   - The real flags are `--budget-mb`, `--no-completion`, `--predict`,
     `--stats-json`, `--policy`, `--ops`, `--source`, `--pid`,
     `--interval-us` and `--duration-s` (see `moepagerd --help`).
   - Hysteresis, re-plan cadence and exploration are only `ResidencyConfig`
     fields in code today, with no CLI flags.
   - The "High churn warning" in TROUBLESHOOTING does not exist.
2. **docs/TROUBLESHOOTING.md:** "Ensure `--willneed-chunk-kb 128` is active" —
   that flag exists only on `moepager fault-io`. The daemon always uses
   `mp_os::WILLNEED_CHUNK` (128 KiB).
3. **docs/EBPF_TRACING.md:** the arguments are reversed and incomplete.
   - The script takes `<inode> <kdev>`, not `<device_id> <inode_number>`.
   - `kdev` must be the *kernel* device (`MKDEV(major, minor)`) as printed
     by `moepager record <model> --print-bpftrace-args`. On btrfs it
     differs from `stat()`.
   - Ingest is `moepager ingest-bpftrace out.txt --gguf model.gguf -o pages.mpt`.
4. **docs/TRACE_FORMAT.md:**
   - `moepager trace-csv trace.mpt -o trace.csv` should be
     `moepager trace-csv trace.mpt > trace.csv`; there is no `-o`.
   - `page2expert … --map map.json` should be `--gguf model.gguf`.
5. **docs/RUNBOOK_GO_NO_GO.md:** the ext4 loopback guide creates
   `/tmp/ext4_scratch.img` and copies an 18.6 GB model into it.
   - On Fedora `/tmp` is **tmpfs (RAM)**, so on a 15 GB laptop this
     exhausts memory.
   - Put the image on disk (e.g. `~/ext4_scratch.img`).
   - Note that it needs `sudo mount`.
6. **docs/FAQ.md, docs/DEPLOYMENT_DOCKER.md:** `docker run … moepager:latest`
   — there is no Dockerfile or image.
7. **docs/PRIVILEGES.md:**
   - The systemd *user* unit with `AmbientCapabilities=CAP_IPC_LOCK
     CAP_SYS_NICE` cannot work. A per-user systemd instance cannot grant
     capabilities the user doesn't have. It must be a system unit with
     `User=`.
   - The Yama advice is also wrong: `ptrace_scope` restricts
     `PTRACE_MODE_ATTACH`, while `process_madvise` checks
     `PTRACE_MODE_READ`.
   - The AppArmor/SELinux section is untested speculation; mark it so or
     remove it.

## Descriptions that don't match the implementation

8. **docs/SYNTHETIC_WORKLOADS.md:** there is no Dirichlet smoothing and no
   Dirichlet-Multinomial urn in `mp-synth`. What it actually does:
   - per-layer Zipf popularity over a random permutation (default skew
     0.9);
   - token reuse: with probability `reuse`, copy an expert used by that
     layer on the previous token;
   - cross-layer affinity: each expert has `affinity_fanout` preferred
     successors, drawn Zipf(`affinity_skew`);
   - optional drift.
9. **docs/GLOSSARY.md "EWMA access rate":** `OnlineStats` is not an EWMA of
   an indicator. The rate is (decayed misses) ÷ (decayed tokens of
   exposure while unpinned), with pinned units frozen and light shrinkage
   to a prior. The LaTeX is also broken (`half-life $ tokens`, `(e)$`).
   FAQ has the same broken LaTeX (`top-$ sparsity`).
10. **docs/SIMULATOR_DESIGN.md:** "clock advances by compute in parallel
    with I/O" is wrong for demand misses, which **stall** the clock. Only
    prefetches overlap compute.
11. **docs/ARCHITECTURE_DEEP_DIVE.md:** "t_fault ~15–30 µs" is unsourced.
    The simulator default is 100 µs, and both are guesses until measured.
12. **docs/adr/0005:**
    - The condition is `len >= 2*GUARD + 2*page`, not `len >= 2*GUARD`.
    - "eliminating false-positive activations" is too strong. btrfs's
      4 MiB readahead and compressed extents still spill into neighbours
      (PHASES known issues).
    - "Verified on real … layouts" only means sentinels were computed from
      real headers, not validated at runtime.

## Unmeasured or overstated claims

13. **docs/MODEL_COMPATIBILITY.md:**
    - "Verified" means only that the *header maps* parse; nothing has been
      run.
    - DeepSeek and Mixtral "Supported" are untested. Older Mixtral GGUFs
      use per-expert 2-D tensors (`blk.N.ffn_gate.E.weight`), which the
      mapper does not group.
    - Use "header-mapped" and "untested".
14. **docs/STORAGE_CHARACTERISTICS.md:**
    - "4 KiB to 128 KiB read-around" contradicts our measurement: 4 MiB on
      btrfs, with 3.3–3.7× amplification.
    - "drive-rated (>2.5 GB/s)" is not what we measured (2.37–2.46 GB/s
      with chunked WILLNEED).
15. **docs/INTEGRATION_OLLAMA.md:**
    - "MAP_PRIVATE creates copy-on-write anonymous pages" is wrong for
      read-only access. Private file pages stay page-cache pages until
      written.
    - Ollama bundles llama.cpp with repacking on, so the `-nr` requirement
      applies and is not exposed. Untested.
16. **docs/TUNING_GUIDE.md:** the "optimal 4–6 G" profiles and cloud VM
    advice are not based on any measurement.
17. **docs/FAQ.md:** "does not alter the mathematical output of llama.cpp".
    moepager itself doesn't, but `--no-repack` switches to different CPU
    kernels, so outputs can differ slightly from *default* llama.cpp.
18. **ADR dates:** 0006 is dated 2026-09-06 and 0009/0010 are dated
    2026-09-09/10. Those are before the project started (2026-09-24) and
    before those findings existed (2026-09-30). Fix them for consistency
    with the history.

## Fine as is
- METRICS_DICTIONARY, MEMORY_PRESSURE, OFFLINE_ANALYSIS, GGUF_SPECIFICATION
  and BENCHMARKING_GUIDE (accurate, if brief).
- The ARCHITECTURE sequence diagram and the CONTRIBUTING checklist.
