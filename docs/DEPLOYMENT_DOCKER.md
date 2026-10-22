# Containers

**Not packaged and untested:** there is no Dockerfile or image. Notes for
anyone trying it:

- The measured configuration (`--budget-mb 0`, no demotion) needs **no**
  extra capabilities. Optional pinning needs `--cap-add=IPC_LOCK` (or a
  raised memlock limit), and optional demotion needs `--cap-add=SYS_NICE`
  plus the engine's pid in the same PID namespace.
- The daemon must see the **same file** (same inode) as the engine and run
  in the **same memory cgroup**. That usually means the same container, or
  a shared cgroup.
- Keep models on a host bind mount rather than an overlayfs layer, so that
  `mincore`/`fadvise` act on the real file's page cache.
