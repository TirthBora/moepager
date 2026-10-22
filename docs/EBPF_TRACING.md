# eBPF Tracing Guide

Instructions for root/privileged tracing using `bpf/moepager.bt` and kernel tracepoints.

## Monitored Tracepoints
- `tracepoint:filemap:mm_filemap_add_to_page_cache`: Captures every insertion into the page cache, recording folio orders, page frame numbers, and device/inode identifiers.
- `tracepoint:filemap:mm_filemap_delete_from_page_cache`: Monitors folio evictions.

## Inode and Device Filtering
To avoid tracing unrelated system processes (e.g. desktop services, browsers), filter by inode and the **kernel** device number. On btrfs, the device that `stat()` reports differs from the superblock device the tracepoints use, so take both values from moepager:
```bash
moepager record model.gguf --print-bpftrace-args      # prints: <inode> <kdev>
sudo bpftrace bpf/moepager.bt <inode> <kdev> > out.bt
moepager ingest-bpftrace out.bt --gguf model.gguf -o pages.mpt
moepager page2expert pages.mpt --gguf model.gguf -o experts.mpt
```
*Untested on real hardware: needs root and bpftrace.*
