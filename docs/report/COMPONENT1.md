# Component 1 — corrections to the original draft

The final title, abstract and architecture are in
[`sections/01-title-abstract.md`](sections/01-title-abstract.md), and the
full report is [`REPORT.md`](REPORT.md) / [`REPORT.pdf`](REPORT.pdf). This
file keeps the list of corrections made to the first draft of Component 1,
and why.

## Corrections to the earlier draft (and why)

| Draft said | Problem | Now |
|---|---|---|
| "Empirical validation demonstrates that moepager completely eliminates read amplification … 2.42 GB/s (5.3×) … confirming that page caches can serve as zero-copy MoE tiers" | Those numbers come from the `fault-io` **microbenchmark** of the readahead *mechanism* on a synthetic file. moepager had not been run with llama.cpp. Claiming end-to-end validation would be false. | Stated as a microbenchmark result; end-to-end numbers come from Phase 7 |
| "Uncoordinated demand faults trigger read-around (up to 4 MiB)" as a general fact | 4 MiB is the **btrfs** readahead window on our laptop. A typical ext4/NVMe setup uses 128 KiB. | Attributed to btrfs on the test machine |
| "block-device-aligned 128 KiB chunks" | The chunks avoid the kernel's **silent truncation** of a single WILLNEED to `max(io_pages, ra_pages)`; alignment is not the point. | Reason stated correctly |
| "catastrophic performance collapse" | Not measured. | Removed |
| "Existing optimization methods require invasive forks" | Most are in-engine patches or open PRs (llama.cpp #25294, #26824, …). They change the engine, but "forks" overstates it. | "require modifying the engine" |
| `mlock` / `MADV_COLD` presented as working parts | Implemented but **untested on real hardware**: pinning needs `CAP_IPC_LOCK` beyond the 8 MB default limit, and demotion needs `CAP_SYS_NICE`. | Marked as optional, privilege-dependent |
| "exposure-compensated marginal utility estimator" | Fine, but it should say what it is: the miss rate per token while unpinned, with pinned units frozen. | Explained |
| Title "Virtual Memory Page-Cache Orchestration" | Acceptable. "Page-cache management" is more precise, since moepager hints the page cache and doesn't manage virtual memory mappings. | Adjusted |
