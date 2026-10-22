# 4. Methodology and Algorithms

## 4.1 Expert map (from the GGUF header)

A GGUF file starts with a header listing every tensor's name, shape, type
and offset. `mp-gguf` parses only this header, so a truncated prefix of a
file works too. For each tensor named `blk.<L>.<ffn_up|ffn_gate|ffn_down|ffn_gate_up>_exps.weight`
with shape `[ne0, ne1, n_expert]`:

- `bytes = type_size × ne0·ne1·n_expert / block_size`, using the ggml type
  table.
- Expert `e`'s slice is `[offset + e·bytes/n_expert, offset + (e+1)·bytes/n_expert)`.
  Each expert is contiguous inside a tensor because the expert index is the
  outermost dimension.
- 2-D per-expert tensors (`*_exps.bias`, `[ne0, n_expert]`) contribute one
  row per expert.

The slices of the same (L, e) are grouped into a **unit**, the granule for
everything that follows. Slices are 32-byte aligned rather than page
aligned, so a boundary page can belong to two units. A reverse page→unit
index (sorted intervals plus a prefix-maximum array) handles this in
O(log n).

For each slice, one **sentinel page** is chosen in its middle, at least
128 KiB from either edge, so that a neighbour's 128 KiB read-around cannot
trigger it.

## 4.2 Black-box observation

The daemon maps the model file read-only and periodically calls `mincore()`
on the sentinel pages. This works without root, even for files it doesn't
own. A sentinel changing from non-resident to resident means "unit
(L, e) has just been faulted in", which is a **miss**.

Hits are invisible to user space: pages already mapped are read through the
CPU's page tables with no kernel involvement. All online statistics are
therefore computed from misses only (Section 4.4).

Token boundaries are not visible either. They are inferred from layer
order: a new token starts when the observed layer drops below the running
maximum minus a slack. With root, the same events can be taken from the
`filemap:mm_filemap_add_to_page_cache` tracepoint (bpftrace script
included).

## 4.3 Expert-completion readahead (the main mechanism)

When a miss of unit u is observed, the daemon immediately issues
`posix_fadvise(WILLNEED)` over **all** slices of u, so the parts the engine
has not reached yet (typically the gate and down slices, hundreds of MB
away) arrive as large asynchronous reads instead of many small synchronous
faults. Two details turned out to be essential:

1. **Chunking.** The kernel silently truncates a single WILLNEED to
   `max(bdi->io_pages, ra->ra_pages)` pages (`force_page_cache_ra()` in
   `mm/readahead.c`). We found this when CI populated exactly 256 KiB of a
   1 MiB request. Requests are therefore issued in 128 KiB chunks.
2. **Refractory window.** Each unit has three sentinels, and the daemon's
   own prefetch flips the other two. Repeated misses of a unit within
   20 ms are ignored so that each real miss causes one prefetch.

The mechanism is prediction-free: it acts only on experts the router has
already chosen.

## 4.4 Residency policy V(e) (optional, privilege-dependent)

For pinning with `mlock`, each unit e of size s_e gets a value per byte:

    V(e) = rate(e) · (t_fault + s_e / B_demand) / s_e

Units are pinned greedily in decreasing V order until the pin budget is
used up. Hysteresis (a newcomer must beat a pinned unit by 25 %) limits
churn.

Because only misses are observable, rate(e) is defined as the **miss rate
per token while unpinned**: decayed miss count ÷ decayed number of tokens
the unit was exposed (not pinned). That is exactly the counterfactual a pin
decision needs. While a unit is pinned it produces no misses and no
exposure, so its estimate is frozen. A small random fraction of pinned
units is released at each re-plan ("exploration") to refresh stale
estimates. Exponential decay uses the scaled-weight trick, with periodic
renormalisation, for O(1) updates.

Demotion of cold units uses `process_madvise(pidfd, MADV_COLD)` on the
engine's mapping, because `fadvise(DONTNEED)` cannot evict pages another
process has mapped (verified experimentally).

## 4.5 Simulation and the optimal bound

`mp-sim` replays a routing trace against a byte-capacity cache:
- A hit costs nothing.
- A miss is queued on a FIFO I/O channel, costing `t_fault + s/B_demand`,
  or `s/B_bulk` when completion readahead is on.
- Compute time per layer is taken from the trace.

Policies: LRU, LFU, prefix pinning, an oracle static-frequency pin, V(e)
with full or miss-only observation, predictive prefetch, and **Belady\***
(evict the unit whose next use is furthest in the future). Belady\* is the
optimal bound for equal sizes and the standard heuristic for variable
sizes.

Independently, `mp-analyze` computes reuse distances with a Fenwick tree in
O(N log N) and derives the **exact LRU miss-ratio curve**. An access hits
in a byte-capacity LRU cache iff (bytes of distinct units since its last
use) + its own size ≤ capacity. The simulator's LRU agrees with this curve
exactly, a cross-check between two independent implementations.

## 4.6 Experimental method (go/no-go)

Kill criteria were fixed before measuring (IDEA_REVIEW §6):
- **K1:** Belady\* cannot save at least 15 % of misses relative to the
  kernel.
- **K2:** fault-driven bandwidth is at least 0.75× bulk bandwidth.
- **K3:** default llama.cpp beats every page-cache configuration.

Experiments:

- **A: routing.** Record real routing with `moe_trace`, a 170-line
  experiment-only tool that reads llama.cpp's `ffn_moe_topk-<layer>`
  tensors through the scheduler's eval callback. Three prompts (chat, code,
  prose) × 255 decode tokens on OLMoE, then analyze and simulate.
- **B: real-kernel replay.** Replay the prose trace on the real model file
  under a cgroup v2 memory wall (`systemd-run --user -p MemoryMax=…`,
  swap 0). Measure SSD bytes (`/proc/self/io`), major faults and time.
- **C: bandwidth microbenchmark.** Read cold expert-sized units of the real
  file through mmap faults, chunked WILLNEED, or pread.
- **D: end to end.** `llama-bench` decode throughput under the same memory
  walls, for:
  - B0: default llama.cpp;
  - B2: `--repack 0`;
  - D2: B2 + moepagerd;
  - P: B2 + read-around off;
  - PD: P + moepagerd.

  Daemon and engine always share the cgroup, so page-cache charges count
  against the same budget.
