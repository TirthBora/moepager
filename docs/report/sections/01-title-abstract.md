# moepager

## Expert-Aware Page-Cache Management for Mixture-of-Experts LLM Inference on Memory-Constrained Linux Machines

**Formal title:** System and Method for Non-Invasive, Expert-Aware
Page-Cache Management for Mixture-of-Experts Model Inference on
Memory-Constrained Linux Hosts

**Repository:** https://github.com/shreshth006/moepager · team fork:
https://github.com/AyushSinha2210/moepager (branch `ayush`)

# 1. Title, Abstract and Architecture

## 1.1 Abstract

Mixture-of-Experts (MoE) language models activate only a few "expert"
sub-networks per layer for each token: 8 of 64 in OLMoE-1B-7B, 4 of 32 in
gpt-oss-20b, 8 of 128 in Qwen3-30B-A3B. That makes them attractive for
consumer machines, which run them with llama.cpp in `mmap` mode when the
model is larger than free RAM, leaving the Linux page cache to decide
which weights stay in memory.

We show that the kernel's generic behaviour fits this workload poorly:
- **Scattered units.** One expert's weights are three slices in three
  tensors hundreds of megabytes apart.
- **Read-around pollution.** On a page fault, the kernel reads a window
  around the faulting page: 4 MiB on our test machine's btrfs. That pulls
  unused neighbouring experts into the cache and multiplies SSD traffic
  3.7–4.8×.
- **Repacking.** llama.cpp's default CPU weight repacking moves 77–100 % of
  expert bytes into anonymous memory, outside the page cache.

We present **moepager**, a Rust toolchain and an unprivileged user-space
daemon that manage the shared page cache at the granularity of
(layer, expert) units, without changing the inference engine's code or
binary:
1. It parses the GGUF header into an exact expert map.
2. It observes expert misses black-box by polling sentinel pages with
   `mincore()`.
3. It answers each miss with **expert-completion readahead**: bulk
   `posix_fadvise(WILLNEED)` of the expert's remaining slices in 128 KiB
   chunks. The chunking is necessary because the kernel silently truncates
   larger requests.
4. A 64-line `LD_PRELOAD` shim turns off fault read-around on the model
   mapping, which no stock llama.cpp option does on single-node machines.

The evaluation toolchain records ground-truth routing from llama.cpp,
computes exact LRU miss-ratio curves, simulates policies against a Belady
optimum, and runs llama.cpp under cgroup memory limits.

On a 15 GB laptop running OLMoE-1B-7B (4.2 GB) under memory limits of
1.5 GB and 2.5 GB, moepager's configuration decoded **1.40 and 3.21
tokens/s**, versus 0.35 and 0.87 tokens/s for the same engine served by
the default page cache. That is **3.7–4.0× faster**, with **7–8× less SSD
traffic** and memory-pressure stall reduced from 12.7 % to 1.0 %. Default
llama.cpp, by contrast, was OOM-killed at both limits unless allowed to
spill 2.3–2.5 GB into compressed RAM swap. On a second model, gpt-oss-20b
(12.1 GB), replaying real routing under 4 GB and 6 GB limits showed the
same pattern: 1.6–2.0× faster than the kernel default, with 15–20× less
SSD traffic.

We also report what did not work: a value-based pinning policy captured
little of the optimal policy's 34–74 % miss reduction, and completion
readahead without turning off read-around made performance worse.

## 1.2 System architecture

![moepager architecture](architecture-portrait.png)

*Figure 1. moepager architecture (source: `docs/report/architecture-portrait.dot`; a landscape version is `architecture.png`).*

- **Offline, once per model:** `mp-gguf` reads only the GGUF header and
  builds the expert map: each (layer, expert) unit's three slice offsets
  and sentinel pages.
- **Runtime:**
  - The unmodified engine runs with `--no-repack` (so weights stay in the
    page cache) and with the `LD_PRELOAD` shim, which turns off read-around
    on the model mapping.
  - Its page faults bring expert slices into the shared page cache.
  - `moepagerd` polls sentinel pages, so it sees misses only. It updates
    `mp-core` statistics and issues page-cache hints through `mp-os`:
    - chunked WILLNEED for completion readahead (no privilege needed);
    - optional `mlock` pinning (`CAP_IPC_LOCK`);
    - optional `MADV_COLD` demotion (`CAP_SYS_NICE`).
  - No weights are copied: engine and daemon share the same page-cache
    pages.
  - Both run inside one cgroup v2 scope with a `memory.max` budget, so
    every page is charged to the same budget.
- **Evaluation:**
  - `moe_trace`, an experiment-only llama.cpp eval callback, records
    ground-truth routing.
  - `mp-analyze` and `mp-sim` evaluate policies against Belady\*, using
    the same `mp-core` code as the daemon.
  - `bench/` runs real-kernel replays, the bandwidth microbenchmark and
    llama-bench under memory limits.
