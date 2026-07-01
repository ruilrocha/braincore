# Search Strategies

All strategies implement `ISearchStrategy` and are swappable at runtime without stopping playback.
The three strategies are exposed as `SearchStrategy::VpTree`, `Closest`, and `Synaptic` in
`BrainEngineTypes.h`.

---

## VpTreeSearch *(default)*

**O(log N) nearest-neighbour via a Vantage-Point tree.**

Queries the precomputed VP-tree to find the closest brain block to the target fingerprint
without scanning all N blocks.  Produces the same result as `ClosestSearch` in virtually
all cases but is dramatically faster for large brains (> ~2000 blocks).

Both the VP-tree (`kNearest`) and the precomputed synapse table (`neighbors`) share the same
`NearestNeighbourIndex` built by `Brain::buildIndex()` — there is no separate build step for
VpTree vs Synaptic.

Used by default.

---

## ClosestSearch

**O(N) brute-force scan.** Evaluates every block in the brain.

Useful for debugging or very small brains (< ~500 blocks) where tree overhead dominates.
For large corpora, prefer VpTreeSearch.

---

## SynapticSearch

**O(K) deterministic walk through the precomputed K-NN synapse graph.**

Instead of scanning all blocks or querying the VP-tree, it evaluates only the K precomputed
nearest-neighbours (synapses) of the current block.  This produces output that evolves
smoothly through timbral space rather than jumping freely.

Requires `buildIndex()` to have been called (`Brain::hasIndex()` must be true).

Key param: `num_synapses` — controls K (neighbourhood size) at build time.

Best for: smooth, flowing transitions through large corpora.

---

## Shared parameters

All three strategies respect the full `SearchParams` scoring:

| Parameter | Effect |
|-----------|--------|
| `mfcc_weight` / `mel_weight` / `spectral_weight` / `chroma_weight` | Per-feature contribution to the distance score; weights are normalised internally |
| `n_ratio` | 0 = amplitude-sensitive, 1 = amplitude-invariant (normalised fingerprints) |
| `stickyness` | Bias toward the next sequential block for temporal coherence |
| `usage_weight` | "Novelty" — penalise frequently-used blocks |
| `usage_falloff` | "Boredom" — how fast the usage penalty decays per step |
| `brightness_target` / `brightness_weight` | Bias block selection by spectral brightness (0=bass, 1=treble) |

---

## Comparison

| Strategy | Speed | Best for |
|----------|-------|----------|
| VpTree | O(log N) | **Default — all modes** |
| Closest | O(N) | Debugging, tiny brains (< 500 blocks) |
| Synaptic | O(K) | Smooth, connected traversal of large brains |

`N` = total blocks, `K` = synapse count (default 50, set via `setNumSynapses()`).
