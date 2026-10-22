# Model Architecture Compatibility Matrix

What has actually been checked for each architecture. "Header-mapped" means
`moepager gguf-map` parsed the real file header and produced an expert map
with no warnings. "Run" means the model was used end to end in Phase 7.

| Architecture | Example model (file) | Expert slices | Bias tensors | Status |
|---|---|---|---|---|
| OLMoE | OLMoE-1B-7B-0924 Q4_K_M | up, gate, down | none | **Run** (Phase 7: routing capture, replay, llama-bench) |
| Qwen3-MoE | Qwen3-30B-A3B Q4_K_M | up, gate, down | none | Header-mapped (`data/maps/`), not run |
| GPT-OSS | gpt-oss-20b MXFP4 | up, gate, down | 2-D per-expert bias rows | Header-mapped (`data/maps/`), not run |
| DeepSeek-V2/V3, Mixtral, others | — | — | — | **Untested.** The mapper groups any `blk.N.*_exps.*` 3-D tensor (and 2-D `_exps` tensors) by expert. Older Mixtral GGUFs store experts as separate 2-D tensors (`blk.N.ffn_gate.E.weight`), which are *not* grouped |

On x86 with AVX2, default llama.cpp repacks Q4_0/Q4_K/IQ4_NL/MXFP4 experts
into anonymous memory (OLMoE and Qwen3 Q4_K_M: 77 % of expert bytes;
gpt-oss MXFP4: 100 %). Run the engine with `-nr`; `gguf-map` warns.
