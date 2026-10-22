# Simulator Design & Virtual Clock Architecture

A technical deep dive into `mp-sim`, its virtual clock coordination, and event progression.

## Virtual Simulation Loop
The simulator processes `.mpt` access traces with a deterministic virtual clock:
1. For each token and layer, accessed units are checked against the simulated residency state.
2. Hits increment hits counters without adding stall latency.
3. Misses submit a read request to the FIFO I/O channel model.
4. After a layer's accesses, the clock advances by its compute time $t_{\text{layer}}$. A demand miss **stalls** the clock until its read completes; only prefetches (issued before the compute step) overlap with compute. All reads share one FIFO I/O channel, so bad prefetches delay demand misses.
