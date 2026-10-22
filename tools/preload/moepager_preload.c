// moepager_preload: turn off fault read-around on GGUF model mappings.
//
// LD_PRELOAD=libmoepager_preload.so llama-cli -m model.gguf -nr ...
//
// The engine's source and binary are untouched: this shim wraps mmap() and,
// for read-only shared mappings of files ending in ".gguf" (or any file of
// at least MOEPAGER_PRELOAD_MIN_MB when MOEPAGER_PRELOAD_ANY=1), applies
// madvise(MADV_RANDOM). That sets VM_RAND_READ on the mapping, so a page
// fault reads only the faulting page instead of a read-around window that
// is 4 MiB on btrfs and pulls neighbouring, unused experts into the cache.
// moepagerd's expert-completion readahead then fetches the rest of each
// expert in bulk. Set MOEPAGER_PRELOAD_QUIET=1 to silence the log line.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

typedef void *(*mmap_fn)(void *, size_t, int, int, int, off_t);

static int wants(int fd, size_t len, int prot, int flags) {
    if (fd < 0 || !(flags & MAP_SHARED) || (prot & PROT_WRITE)) return 0;
    char link[64], path[PATH_MAX];
    snprintf(link, sizeof link, "/proc/self/fd/%d", fd);
    ssize_t n = readlink(link, path, sizeof path - 1);
    if (n <= 0) return 0;
    path[n] = 0;
    const char *any = getenv("MOEPAGER_PRELOAD_ANY");
    if (any && *any == '1') {
        const char *mb = getenv("MOEPAGER_PRELOAD_MIN_MB");
        size_t min = (size_t) (mb ? atol(mb) : 64) << 20;
        return len >= min;
    }
    return n > 5 && strcmp(path + n - 5, ".gguf") == 0;
}

static void *wrap(mmap_fn real, void *addr, size_t len, int prot, int flags, int fd, off_t off) {
    void *p = real(addr, len, prot, flags, fd, off);
    if (p != MAP_FAILED && wants(fd, len, prot, flags)) {
        int rc = madvise(p, len, MADV_RANDOM);
        const char *q = getenv("MOEPAGER_PRELOAD_QUIET");
        if (!(q && *q == '1')) {
            fprintf(stderr, "moepager_preload: MADV_RANDOM on %zu MiB mapping (fd %d): %s\n",
                    len >> 20, fd, rc == 0 ? "ok" : "failed");
        }
    }
    return p;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
    static mmap_fn real;
    if (!real) real = (mmap_fn) dlsym(RTLD_NEXT, "mmap");
    return wrap(real, addr, len, prot, flags, fd, off);
}

void *mmap64(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
    static mmap_fn real;
    if (!real) real = (mmap_fn) dlsym(RTLD_NEXT, "mmap64");
    return wrap(real, addr, len, prot, flags, fd, off);
}
