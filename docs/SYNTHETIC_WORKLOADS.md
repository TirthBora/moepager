# Synthetic Workload Generation

How `mp-synth` generates synthetic MoE routing traces (`moepager synth`).
These traces exercise the tooling only; Phase 7 results use real routing
captured with `tools/moe_trace`.

## Per (token, layer)

`top_k` distinct experts are drawn slot by slot from a mixture of three
mechanisms:
1. **Token-to-token reuse** (probability `reuse`): a random expert this
   layer used on the previous token.
2. **Cross-layer affinity** (probability `affinity`): pick a random expert
   chosen at layer l−1 for this token, then one of its `affinity_fanout`
   preferred successors, drawn with Zipf(`affinity_skew`).
3. **Popularity** (otherwise): a Zipf(`skew`) draw over a random per-layer
   permutation of the experts.

Duplicates are redrawn (up to 64 tries, then the most popular unused
expert).

## Other knobs

- `drift_every` / `drift_frac`: every N tokens, re-shuffle a fraction of
  each layer's popularity order (domain shift).
- `t_layer_ns`, `jitter`, `t_token_overhead_ns`: timing. Normal jitter per
  layer, plus a fixed per-token overhead.
- Defaults: 24 layers × 64 experts, top-8, 256 tokens, skew 0.9, reuse 0.3,
  affinity 0.3, fanout 4.

The generator uses its own SplitMix64/xoshiro256\*\* RNG, so traces are
bit-identical across dependency upgrades. There is no Dirichlet smoothing
or urn model.
