# 7. Conclusion

## 7.1 Summary

We built moepager, a toolchain and daemon that manages the Linux page cache
for Mixture-of-Experts inference using the model file's structure, without
changing the inference engine's code.

The project was run as an experiment with kill criteria fixed in advance.
On a real model (OLMoE-1B-7B) on a 15 GB laptop, the outcome was:

- **Go on the mechanism, no-go on the original emphasis.** We expected
  smarter *eviction* to be the main lever. Measurement showed the dominant
  cost under memory pressure is the kernel's fault **read-around**. On this
  machine's btrfs it read 3.7× more data than needed and filled the cache
  with unused neighbouring experts.
- **What works:**
  - turning read-around off for the model mapping, with a 64-line
    `LD_PRELOAD` shim, since no stock llama.cpp option does this on
    single-node machines;
  - plus moepagerd's expert-completion readahead in 128 KiB chunks.

  Together they turn tens of thousands of 4 KiB faults per token into bulk
  reads. See Section 6.5 for the end-to-end numbers.
- **What doesn't (yet):**
  - Completion readahead *without* turning read-around off made things
    worse.
  - Our V(e) pinning policy only matched LFU when allowed to pin most of
    the cache. With black-box (miss-only) observation it still loses
    10–40 % in most cells, up to 2×, and it needs extra privileges.
  - Default llama.cpp is faster than every page-cache configuration
    *if* it may spill 2.3–2.5 GB of repacked weights into zram (compressed
    RAM). Without swap it is OOM-killed at both budgets.
- **Smaller findings with independent value:**
  - The kernel silently truncates `WILLNEED`.
  - llama.cpp's default repacking moves 77–100 % of expert bytes out of
    the page cache.
  - llama.cpp's `--lazy-mode` keeps weights in anonymous memory.
  - btrfs reports different device numbers in `stat()` and
    `/proc/pid/maps`.

## 7.2 Limitations

- One machine, one filesystem (btrfs with a 4 MiB readahead window), one
  model (OLMoE). Qwen3-30B and gpt-oss-20b were mapped from their headers
  but not run, for lack of disk space and time.
- Short runs (32 decode tokens × 3 repetitions in llama-bench); some
  configurations show large run-to-run spread.
- A desktop session was running during measurements.
- Pinning (`mlock`), demotion (`MADV_COLD`) and eBPF observation are
  implemented but untested, because they need privileges.
- The shim is still an intervention in the engine's process, although it
  doesn't change its code or binary.

## 7.3 Future work

1. Upstream: propose a llama.cpp option to apply `MADV_RANDOM` to expert
   tensors on single-node machines, which would remove the need for the
   shim.
2. Repeat on ext4 (128 KiB readahead) and on the larger models, with
   longer runs.
3. Evaluate pinning end to end with `CAP_IPC_LOCK` and a pin budget near
   90 % of the expert cache. Narrow the miss-only gap with privileged hit
   observation (DAMON) where available.
4. Add a fallback when WILLNEED is ineffective (`MADV_POPULATE_READ`), and
   PSI-driven control of the memory budget to protect other applications.

# 8. References

1. A. Eliseev, D. Mazur. *Fast Inference of Mixture-of-Experts Language Models with Offloading.* arXiv:2312.17238, 2023.
2. L. Xue et al. *MoE-Infinity: Efficient MoE Inference on Personal Machines with Sparsity-Aware Expert Cache.* arXiv:2401.14361, 2024.
3. *HOBBIT: A Mixed Precision Expert Offloading System for Fast MoE Inference.* arXiv:2411.01433, 2024.
4. K. Alizadeh et al. *LLM in a flash: Efficient Large Language Model Inference with Limited Memory.* ACL 2024 (arXiv:2312.11514).
5. Y. Si, Y. Lin, D. Li, J. Zhang. *Who Should Own the Expert Cache? Kernel-Managed Tiering for Trillion-Parameter MoE Inference.* arXiv:2608.12103, 2026.
6. T. Zussman, I. Zarkadas, A. Cidon et al. *Cache is King: Smart Page Eviction with eBPF* (cachebpf / cache_ext). arXiv:2502.02750, 2025.
7. *DuoServe-MoE: Dual-Phase Expert Prefetch and Caching for LLM Inference QoS Assurance.* arXiv:2509.07379, 2025.
8. *A Survey on Inference Optimization Techniques for Mixture of Experts Models.* arXiv:2412.14219, 2024.
9. L. A. Belady. *A study of replacement algorithms for a virtual-storage computer.* IBM Systems Journal 5(2), 1966.
10. R. L. Mattson, J. Gecsei, D. R. Slutz, I. L. Traiger. *Evaluation techniques for storage hierarchies.* IBM Systems Journal 9(2), 1970.
11. llama.cpp (ggml-org): source at commit 970b51c (`src/llama-mmap.cpp`, `src/llama-model-loader.cpp`, `ggml/src/ggml-cpu/repack.cpp`); PRs #25294, #26824, #29887, #29250; issue #20757; discussion #27149. https://github.com/ggml-org/llama.cpp
12. Linux kernel: `mm/readahead.c` (`force_page_cache_ra`), `include/trace/events/filemap.h`; man pages `mincore(2)`, `posix_fadvise(2)`, `madvise(2)`, `process_madvise(2)`, `cachestat(2)`.
13. GGUF model files: allenai/OLMoE-1B-7B-0924-GGUF, unsloth/Qwen3-30B-A3B-GGUF, ggml-org/gpt-oss-20b-GGUF (Hugging Face).
