# Troubleshooting & Diagnostics

Common issues when running `moepager` and `moepagerd`, with the real
messages and flags.

## 1. `warning: pid N maps the model with fault read-around on`
- **Cause:** the engine's model mapping lacks `MADV_RANDOM` (no `rr` in
  `/proc/<pid>/smaps` VmFlags). Every fault then reads a whole readahead
  window (4 MiB on btrfs) of mostly unused neighbouring experts.
- **Fix:** start the engine with
  `LD_PRELOAD=tools/preload/libmoepager_preload.so`. It prints
  `moepager_preload: MADV_RANDOM on … mapping: ok`.

## 2. Default llama.cpp is OOM-killed under a memory limit
- **Cause:** with repacking (the default on AVX2/NEON), expert weights live
  in anonymous memory, which with swap off cannot be reclaimed.
- **Fix:** run llama.cpp with `-nr` (`--repack 0` in llama-bench) so weights
  stay in the page cache. `moepager gguf-map model.gguf` prints how much
  would be repacked.
- llama.cpp's `--lazy-mode on` does **not** help. It also keeps weights in
  anonymous memory and was OOM-killed in our runs.

## 3. `cachestat` returns `EPERM`
- **Cause:** Linux `cachestat` requires owning, or being able to write, the
  file.
- **Effect:** none for the daemon, which uses `mincore` on its own mapping.
  `mincore` works on any readable file.

## 4. Prefetch seems to load only part of each expert
- **Cause:** the kernel truncates one `posix_fadvise(WILLNEED)` to the
  device's readahead size.
- **Status:** the daemon always issues 128 KiB chunks
  (`mp_os::WILLNEED_CHUNK`). The `--willneed-chunk-kb` flag exists only on
  `moepager fault-io`, for experiments. If WILLNEED populates *nothing*
  (readahead disabled on the device), see PHASES P8.5.

## 5. Pinning does nothing / `budget_clamps` > 0 in the stats
- **Cause:** `RLIMIT_MEMLOCK` (8 MB by default) caps `mlock`.
- **Fix:** grant `CAP_IPC_LOCK` (`docs/PRIVILEGES.md`), or keep
  `--budget-mb 0`, the recommended setting.

## 6. Many more `events` than real expert uses
- **Cause:** read-around on (see 1). Sentinels then fire on collateral
  pages. In one replay that was 331k events for 32k real uses.
- **Fix:** use the shim. Do not try to fix it with `--refractory-ms`; a
  20 ms window doubled misses in our ablation.
