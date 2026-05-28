# Search Strategies

All strategies implement `ISearchStrategy` and can be swapped at runtime without stopping playback.
Strategies that require a synapse graph (Synaptic, Markov) build it lazily on first use.

---

## VpTreeSearch *(default)*

**Approximate nearest neighbour via a vantage-point tree.**

Builds a VP-tree at `buildIndex()` time and answers queries in O(log n). Produces the same
result as `ClosestSearch` in virtually all cases but is dramatically faster for large brains.

Used by default in the Swift iOS app ("Closest (optimised)").

---

## ClosestSearch

**Brute-force O(n) scan.** Evaluates every block in the brain. Useful for debugging or
very small brains where tree overhead dominates.

---

Both VpTree and ClosestSearch share the same parameters:

| Parameter | Effect |
|-----------|--------|
| `mfcc_weight` / `mel_weight` / `spectral_weight` | Relative contribution of each fingerprint to the distance score |
| `n_ratio` | 0 = amplitude-sensitive matching, 1 = amplitude-invariant |
| `stickyness` | Bias toward the next sequential source block |
| `usage_weight` | Penalise frequently-used blocks (novelty) |
| `usage_falloff` | How fast usage penalty decays (boredom) |

---

## SynapticSearch

**Deterministic walk through a pre-computed similarity graph.**

Instead of scanning all blocks, evaluates only the pre-computed nearest neighbours (synapses)
of the current block. Produces output that evolves smoothly through timbral space.

Best for: smooth flowing transitions, large brains where O(n) is too slow.

Key params: `num_synapses` (neighbourhood size), `usage_weight`.

---

## MarkovChainSearch

**Probabilistic walk over the synapse graph.**

Like Synaptic, but samples probabilistically via softmax over synapse distances. More varied
output that still flows through similar timbral regions.

Best for: generative/infinite mode with controlled randomness.

Key params: `temperature` (low = nearly deterministic, high = more random), `num_synapses`.

---

## MomentumSearch

**Velocity-based trajectory through fingerprint space.**

Tracks a velocity vector in fingerprint space. Output drifts smoothly in the direction it
was already moving — like a ball rolling through the brain's timbral landscape.

Best for: infinite mode; produces the smoothest, most cinematic evolution.

Key params: `momentum` (0 = no inertia, 1 = full trajectory), `momentum_decay`.

---

## Comparison

| Strategy | Speed | Best for |
|----------|-------|----------|
| VpTree | O(log n) | **Default — all modes** |
| Closest | O(n) | Debugging, tiny brains |
| Synaptic | O(k) | Smooth large-brain traversal |
| Markov | O(k) | Generative mode with variety |
| Momentum | O(n) | Cinematic infinite mode |

`n` = total blocks, `k` = synapse count (typically 100–1000).
