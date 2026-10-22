# Privileges and safety

`moepager` only **reads** the model file and gives the kernel hints about it.
It never writes model data, and it never touches the engine's memory except
through `process_madvise` advice, which cannot change contents.

## Feature Privilege Matrix

| feature | needs | without it |
|---|---|---|
| sentinel / full-scan `mincore` observation | read access to the model file | — |
| `cachestat` probe | owner of, or write access to, the file (EPERM otherwise, verified on 6.19) | falls back to `mincore` |
| `posix_fadvise(WILLNEED)` prefetch / completion readahead | read access | — |
| pinning (`mlock` on the daemon's own mapping) | `CAP_IPC_LOCK` or `RLIMIT_MEMLOCK` ≥ pin budget (default: 8 MB) | pin budget clamped to the rlimit, warning logged |
| demotion (`process_madvise(MADV_COLD)` on the engine) | `CAP_SYS_NICE` + ptrace-read access to the engine (same user) | disabled; reclaim left to the kernel |
| eBPF filemap tracepoints | root or `CAP_BPF`+`CAP_PERFMON` | sentinel polling |
| MGLRU on/off baseline | root (`/sys/kernel/mm/lru_gen/enabled`) | baseline skipped |
| cgroup budget for experiments | systemd user delegation of `memory` (default on Fedora) | — |

---

## Least-Privilege Setup

Recommended least-privilege setup for a dedicated binary:
```bash
sudo setcap cap_ipc_lock,cap_sys_nice+ep target/release/moepagerd
```
Do **not** run the daemon as root just to get these two capabilities.

### Systemd service setup

A **user** unit (`systemctl --user`) cannot grant capabilities the user
doesn't have, so `AmbientCapabilities=` only works in a **system** unit that
drops to your user. *Untested.*

```ini
# /etc/systemd/system/moepagerd.service
[Unit]
Description=moepager MoE page-cache daemon

[Service]
Type=simple
User=<your-user>
ExecStart=/home/<your-user>/.local/bin/moepagerd /home/<your-user>/models/model.gguf --ops linux --source sentinel --policy v --budget-mb 0
AmbientCapabilities=CAP_IPC_LOCK CAP_SYS_NICE
CapabilityBoundingSet=CAP_IPC_LOCK CAP_SYS_NICE
LimitMEMLOCK=infinity
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

The measured configuration needs **no** capabilities (`--budget-mb 0`, no
demotion). The capabilities are only for the optional pinning and demotion.
Note also that a system service is not in your session's cgroup. For
memory-limited runs, start the daemon next to the engine instead (see
`bench/phase7.sh`).

---

## Troubleshooting Permissions

### 1. `RLIMIT_MEMLOCK` Clamping
- **Symptom**: `moepagerd` prints `warning: RLIMIT_MEMLOCK is N MiB; pin budget clamped`, or `budget_clamps` > 0 in `--stats-json`.
- **Cause**: Unprivileged user accounts are typically restricted to 8 MB of mlock memory (`ulimit -l`).
- **Fix**: Grant `CAP_IPC_LOCK` via `setcap`, or raise limits in `/etc/security/limits.d/99-moepager.conf`:
  ```
  <username> soft memlock unlimited
  <username> hard memlock unlimited
  ```

### 2. `process_madvise` EPERM
- **Symptom**: Demotion actions fail with `Operation not permitted`.
- **Cause**: missing `CAP_SYS_NICE`, or the target belongs to another user (the kernel checks `PTRACE_MODE_READ` access). Yama's `ptrace_scope` restricts only attaching, not this read-mode check.
- **Fix**: run the daemon as the engine's user and grant `CAP_SYS_NICE`. *Demotion is untested on real hardware.*

---

## Risk Analysis

- **Memory Starvation**: Pinning too much memory starves co-tenant applications. The pin budget (`--budget-mb`) must always be bounded well below free memory; the recommended value is 0.
- **Data Integrity**: `moepager` opens files in read-only mode (`O_RDONLY`). Neither `posix_fadvise` nor `process_madvise(MADV_COLD)` can corrupt memory or disk data.


---

## AppArmor & SELinux
Not tested. A confined daemon would at least need read access to the model
file, `capability ipc_lock` / `sys_nice` if pinning or demotion is used,
and permission for the ptrace-read check on the engine for demotion.
