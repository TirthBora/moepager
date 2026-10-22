# Storage Characteristics Under Page-Cache Fault Load

Measured on the dev laptop (Solidigm P41 Plus NVMe, btrfs with
`compress=zstd:1`), Phase 7, `moepager fault-io` on the real OLMoE file
(BENCHMARKS.md, Experiment C).

## Read amplification
- A page fault on an mmap'd file reads a window sized by the backing
  device's readahead. That is **4 MiB on btrfs here** (`/sys/class/bdi/btrfs-1/read_ahead_kb`),
  and 128 KiB on a typical ext4/NVMe setup.
- Faulting cold OLMoE experts read **4.8× more** than needed, at
  0.15–0.24 GB/s.

## Bulk readahead
- `posix_fadvise(WILLNEED)` in 128 KiB chunks delivered 1.49–1.70 GB/s with
  1.00× amplification.
- Single unchunked calls were truncated by the kernel and slower
  (0.97–1.42 GB/s).
- These are effective rates for scattered 3.8 MB units, not the drive's
  sequential rating.

## Wear
- moepager issues only reads and advice (`fadvise`, `madvise`, `mincore`,
  `mlock`), so it adds no NAND writes. Reducing read amplification also
  reduces total bytes read.
