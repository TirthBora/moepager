# Ollama and Other llama.cpp-Based Engines

**Untested.** Notes for anyone trying moepager with engines that embed
llama.cpp.

- **Repacking:** moepager only helps if expert weights stay in the page
  cache. Ollama bundles llama.cpp with its default repacking, which on
  AVX2/NEON moves most quantised expert weights into anonymous memory, and
  it does not obviously expose `--no-repack`. Check with
  `moepager gguf-map <blob>` and by watching `/proc/<pid>/smaps` (file vs
  anonymous RSS).
- **Mapping type:** llama.cpp maps the model with `MAP_SHARED`. A
  read-only `MAP_PRIVATE` mapping would also stay in the page cache: pages
  only become private anonymous copies when written.
- **Read-around:** the `tools/preload` shim recognises files ending in
  `.gguf`. Ollama's model blobs have no extension, so set
  `MOEPAGER_PRELOAD_ANY=1` (optionally `MOEPAGER_PRELOAD_MIN_MB`) to cover
  them.
