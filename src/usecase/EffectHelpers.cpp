#include "EffectHelpers.h"

#include "../domain/Random.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace audio::usecase::effects {

// ── Grain envelope ─────────────────────────────────────────────────────

void applyGrainEnvelope(std::vector<double>& grain) {
    const auto grain_len = grain.size();
    if (grain_len < 4) {
        return;
    }
    for (std::size_t i = 0; i < grain_len; ++i) {
        const double phase = static_cast<double>(i) / static_cast<double>(grain_len - 1);
        grain[i] *= 0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * phase));
    }
}

// ── Grain extraction ───────────────────────────────────────────────────

std::vector<double> extractGrain(const std::vector<double>& src, const std::size_t grain_size,
                                 const std::size_t offset) {
    std::vector<double> grain(grain_size);
    for (std::size_t i = 0; i < grain_size; ++i) {
        grain[i] = src[(offset + i) % src.size()];
    }
    applyGrainEnvelope(grain);
    return grain;
}

// ── Pitch-jittered grain extraction ────────────────────────────────────

static std::vector<double> extractPitchJitteredGrain(const std::vector<double>& src,
                                                     const std::size_t grain_size,
                                                     const std::size_t offset, const double speed) {
    // Determine how many source samples we need to read at `speed`.
    const auto src_len = static_cast<std::size_t>(static_cast<double>(grain_size) * speed);
    if (src_len == 0) {
        return {static_cast<double>(grain_size), 0.0};
    }

    std::vector<double> grain(grain_size);
    const auto src_size = src.size();

    for (std::size_t i = 0; i < grain_size; ++i) {
        // Map output sample i to a fractional source position.
        const double src_pos = static_cast<double>(i) * speed;
        const auto idx0 = static_cast<std::size_t>(src_pos);
        const double frac = src_pos - static_cast<double>(idx0);

        const std::size_t s0 = (offset + idx0) % src_size;
        const std::size_t s1 = (offset + idx0 + 1) % src_size;

        // Linear interpolation.
        grain[i] = (src[s0] * (1.0 - frac)) + (src[s1] * frac);
    }

    applyGrainEnvelope(grain);
    return grain;
}

// ── Granular scatter ───────────────────────────────────────────────────

std::vector<double> granularScatter(const std::vector<double>& src, const std::size_t block_size,
                                    const double grain_size_f, const double scatter,
                                    const double density, const double size_variation,
                                    const double amp_variation, const double pitch_jitter,
                                    const double hop_randomness) {
    const auto base_gs = static_cast<std::size_t>(std::clamp(grain_size_f, 0.01, 1.0) *
                                                  static_cast<double>(block_size));
    if (base_gs == 0 || base_gs >= block_size) {
        return src;
    }

    std::vector output(block_size, 0.0);
    std::vector weight(block_size, 0.0);

    const auto base_hop = static_cast<std::size_t>(
        std::max(1.0, static_cast<double>(base_gs) / std::max(density, 0.1)));

    // Use a double accumulator for position to handle fractional hop randomness.
    double pos_accum = 0.0;

    while (static_cast<std::size_t>(pos_accum) < block_size) {
        const auto pos = static_cast<std::size_t>(pos_accum);

        // ── Per-grain random size ──────────────────────────────────────
        std::size_t gs = base_gs;
        if (size_variation > 0.0) {
            const double var = 1.0 + (size_variation * (rng::randomDouble() - 0.5) * 2.0);
            gs = std::clamp(static_cast<std::size_t>(static_cast<double>(base_gs) * var),
                            std::size_t{4}, block_size);
        }

        // ── Per-grain scatter offset ───────────────────────────────────
        const auto max_off = static_cast<int>(src.size() > gs ? src.size() - gs : 0);
        auto offset = static_cast<int>(pos);
        if (scatter > 0.0 && max_off > 0) {
            const auto jitter = static_cast<int>(scatter * static_cast<double>(max_off) *
                                                 (rng::randomDouble() - 0.5) * 2.0);
            offset = std::clamp(offset + jitter, 0, max_off);
        }

        // ── Per-grain pitch jitter (playback speed variation) ──────────
        double speed = 1.0;
        if (pitch_jitter > 0.0) {
            speed = 1.0 + (pitch_jitter * 0.5 * (rng::randomDouble() - 0.5) * 2.0);
            speed = std::clamp(speed, 0.5, 2.0);
        }

        // ── Extract grain (with or without pitch shift) ────────────────
        std::vector<double> grain;
        if (std::abs(speed - 1.0) > 1e-4) {
            grain = extractPitchJitteredGrain(src, gs, static_cast<std::size_t>(offset), speed);
        } else {
            grain = extractGrain(src, gs, static_cast<std::size_t>(offset));
        }

        // ── Per-grain amplitude variation ──────────────────────────────
        if (amp_variation > 0.0) {
            const double amp =
                std::max(0.0, 1.0 + (amp_variation * (rng::randomDouble() - 0.5) * 2.0));
            for (auto& samp : grain) {
                samp *= amp;
            }
        }

        // ── Overlap-add ────────────────────────────────────────────────
        for (std::size_t i = 0; i < gs && pos + i < block_size; ++i) {
            output[pos + i] += grain[i];
            weight[pos + i] += 1.0;
        }

        // ── Advance position with optional hop randomness ──────────────
        auto hop = static_cast<double>(base_hop);
        if (hop_randomness > 0.0) {
            hop *= 1.0 + (hop_randomness * (rng::randomDouble() - 0.5) * 2.0);
            hop = std::max(hop, 1.0);
        }
        pos_accum += hop;
    }

    for (std::size_t i = 0; i < block_size; ++i) {
        if (weight[i] > 0.0) {
            output[i] /= weight[i];
        }
    }
    return output;
}

// ── Stutter ────────────────────────────────────────────────────────────

void applyStutter(std::vector<double>& samples, const double chance, const int count) {
    if (chance <= 0.0) {
        return;
    }
    if (rng::randomDouble() > chance) {
        return;
    }

    const auto num_samples = samples.size();
    const auto seg = num_samples / static_cast<std::size_t>(std::max(count, 2));
    if (seg < 4) {
        return;
    }

    const auto start = rng::randomIndex(num_samples - seg);
    std::vector stutter_seg(samples.begin() + static_cast<std::ptrdiff_t>(start),
                            samples.begin() + static_cast<std::ptrdiff_t>(start + seg));

    // Micro-fade edges to prevent clicks.
    const std::size_t fade = std::min(seg / 8, std::size_t{32});
    for (std::size_t i = 0; i < fade; ++i) {
        const double env = static_cast<double>(i) / static_cast<double>(fade);
        stutter_seg[i] *= env;
        stutter_seg[seg - 1 - i] *= env;
    }

    for (std::size_t pos = 0; pos < num_samples; pos += seg) {
        const std::size_t len = std::min(seg, num_samples - pos);
        std::copy_n(stutter_seg.begin(), static_cast<std::ptrdiff_t>(len),
                    samples.begin() + static_cast<std::ptrdiff_t>(pos));
    }
}

// ── Envelope shaping ───────────────────────────────────────────────────

void applyEnvelope(std::vector<double>& samples, const int shape, const double amount) {
    if (shape <= 0 || amount <= 0.0) {
        return;
    }
    const auto num_samples = samples.size();
    if (num_samples == 0) {
        return;
    }

    for (std::size_t i = 0; i < num_samples; ++i) {
        const double phase = static_cast<double>(i) / static_cast<double>(num_samples - 1);
        double env = 1.0;

        switch (shape) {
            case 1:
                env = std::exp(-4.0 * phase);
                break;
            case 2:
                env = std::exp(-4.0 * (1.0 - phase));
                break;
            case 3:
                env = 0.5 + (0.5 * std::cos(16.0 * std::numbers::pi * phase));
                break;
            case 4:
                env = std::exp(-8.0 * phase) *
                      std::sin(std::numbers::pi * std::min(phase * 10.0, 1.0));
                break;
            default:
                break;
        }
        samples[i] *= (1.0 - amount) + (env * amount);
    }
}

}  // namespace audio::usecase::effects
