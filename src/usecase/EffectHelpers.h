#pragma once

#include <cstddef>
#include <vector>

namespace audio::usecase {

/**
 * Shared audio effect helpers for block-level post-processing.
 *
 * Used by both SoundProcessor (batch) and StreamProcessor (real-time)
 * to avoid code duplication.
 */
namespace effects {

/// Apply a Hann (raised-cosine) envelope to a grain in-place.
void applyGrainEnvelope(std::vector<double>& grain);

/// Extract a Hann-enveloped grain from @p src at @p offset with wrapping.
[[nodiscard]] std::vector<double> extractGrain(const std::vector<double>& src,
                                                std::size_t grain_size,
                                                std::size_t offset);

/**
 * Granular scatter: break a block into overlapping micro-grains with
 * randomised positions, creating granular synthesis textures.
 *
 * @param src          Source block samples.
 * @param block_size   Full block size in samples.
 * @param grain_size_f Grain size as fraction of block_size [0.01, 1.0].
 * @param scatter      Random offset amount [0.0, 1.0].
 * @param density      Grain overlap density [0.1, 4.0].
 * @return             Granularised block of block_size samples.
 */
[[nodiscard]] std::vector<double> granularScatter(const std::vector<double>& src,
                                                   std::size_t block_size,
                                                   double grain_size_f,
                                                   double scatter,
                                                   double density);

/**
 * Stutter: probabilistically repeat a sub-region within the block.
 *
 * @param samples Block samples (modified in-place).
 * @param chance  Probability of triggering stutter [0.0, 1.0].
 * @param count   Number of repetitions [2, 8].
 */
void applyStutter(std::vector<double>& samples, double chance, int count);

/**
 * Per-block amplitude envelope shaping.
 *
 * @param samples Block samples (modified in-place).
 * @param shape   Envelope type: 0=none, 1=decay, 2=swell, 3=tremolo, 4=pluck.
 * @param amount  Blend between flat (0.0) and full envelope (1.0).
 */
void applyEnvelope(std::vector<double>& samples, int shape, double amount);

} // namespace effects
} // namespace audio::usecase

