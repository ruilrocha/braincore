# Search Strategies

brain-io includes four search strategies that determine how target blocks are matched to
brain blocks. All implement `ISearchStrategy::search()` and can be swapped at runtime
via `Brain::setSearchStrategy()`.

---

## ClosestSearch

**The default. Brute-force minimum-distance matching.**

For each target block, scans ALL blocks in the brain and returns the one with the
lowest blended distance score.

### How it works

1. Compute `fullScore()` for every block in the brain:
   - Blend MFCC and spectral distances (via `blend_ratio`)
   - Blend raw and normalised distances (via `n_ratio`)
   - Add usage penalty (via `usage_weight`)
2. Apply stickyness bias (optional sequential block preference)
3. Return the block with the lowest total score

### When to use

- **Target-driven modes** (batch, stream) — produces the most faithful reconstruction
- When you want the output to closely resemble the target's timbral structure
- When the brain is small enough that brute-force scanning is fast

### Key parameters

| Parameter | Effect |
|-----------|--------|
| `blend_ratio` | 1.0 = match by timbre (MFCC), 0.0 = match by spectrum |
| `n_ratio` | 0.0 = amplitude-dependent, 1.0 = amplitude-invariant |
| `stickyness` | Bias toward sequential blocks for coherence |
| `usage_weight` | Penalise repeated blocks (promote variety) |

---

## SynapticSearch

**Deterministic walk through a pre-computed similarity graph.**

Instead of scanning every block, only evaluates the pre-computed nearest neighbours
(synapses) of the *current* block. Produces output that evolves smoothly through
the brain's timbral space.

### How it works

1. Look at the current block's synapse list (N most similar blocks, pre-computed)
2. Among those N candidates, pick the one closest to the target fingerprint
3. Apply usage penalty
4. Step to the chosen block

### When to use

- When you want **flowing transitions** between similar-sounding blocks
- In infinite mode for smooth evolution
- When the brain is large and you want faster search (only evaluates N candidates)

### Key parameters

| Parameter | Effect |
|-----------|--------|
| `num_synapses` | How many neighbours to evaluate (fewer = more constrained) |
| `usage_weight` | Prevent getting stuck in loops |

### Requirements

Requires `Brain::buildSynapses()` — called automatically when this strategy is
selected at runtime.

---

## MarkovChainSearch

**Probabilistic walk over the synapse graph with temperature control.**

Like SynapticSearch, but instead of deterministically picking the best synapse,
it samples probabilistically using softmax-weighted distances. This produces more
varied output that still flows through similar timbral regions.

### How it works

1. Look at the current block's synapse list
2. Compute distances from each synapse to the target fingerprint
3. Apply softmax with temperature to create a probability distribution
4. Sample one block from that distribution
5. Apply usage penalty

### When to use

- When you want **controlled randomness** — more varied than Synaptic but not chaotic
- For generative/infinite mode with more "surprise"
- When you want to dial creativity up/down via temperature

### Key parameters

| Parameter | Effect |
|-----------|--------|
| `temperature` | Low (0.1) = nearly deterministic, High (5.0) = more random exploration |
| `num_synapses` | Size of the neighbourhood to sample from |
| `usage_weight` | Prevent repetitive loops |

### Requirements

Requires `Brain::buildSynapses()` — called automatically when selected.

---

## MomentumSearch

**Velocity-based trajectory through fingerprint space.**

Tracks a "velocity vector" in fingerprint space. Instead of jumping to the closest match,
it drifts smoothly in the direction it was already moving — like a ball rolling through
the brain's timbral landscape.

### How it works

1. Compute the direction vector from the previous match to the current target fingerprint
2. Blend this with the accumulated velocity (controlled by `momentum`)
3. Apply velocity decay (controlled by `momentum_decay`)
4. Find the block closest to (current position + velocity)
5. Update position and velocity for the next step

### When to use

- **Infinite mode** — produces the smoothest, most cinematic evolution
- When you want output that sounds like one continuous sound rather than discrete jumps
- For ambient/textural generation

### Key parameters

| Parameter | Effect |
|-----------|--------|
| `momentum` | 0.0 = no inertia (behaves like ClosestSearch), 1.0 = full trajectory following |
| `momentum_decay` | 1.0 = no decay (constant speed), 0.0 = instant stop |
| `usage_weight` | Prevent getting stuck in orbits |

---

## Comparison

| Strategy | Speed | Variety | Coherence | Best Mode |
|----------|-------|---------|-----------|-----------|
| Closest | O(n) | Low | High (target-faithful) | Batch, Stream |
| Synaptic | O(k) | Medium | High (smooth flow) | Stream, Infinite |
| Markov | O(k) | High | Medium (probabilistic) | Infinite |
| Momentum | O(n) | Medium | Very High (trajectory) | Infinite |

Where `n` = total blocks in brain, `k` = synapse count (typically 100).
