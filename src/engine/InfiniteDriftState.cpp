#include "InfiniteDriftState.h"

#include "../domain/BlockAnalysis.h"
#include "../domain/Brain.h"
#include "../domain/Random.h"

#include <cmath>

namespace audio {

namespace {

void copyWithNoise(std::vector<float>& dst, const std::vector<float>& src, double noise_range) {
    dst.resize(src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        dst[i] = src[i] + static_cast<float>(rng::randomDouble(-noise_range, noise_range));
    }
}

}  // namespace

void InfiniteDriftState::initFromNoise(const Brain& brain) {
    if (brain.empty()) {
        return;
    }
    const std::size_t seed_idx = rng::randomIndex(brain.blocks().size());
    const AudioPrint& ref = brain.blocks()[seed_idx].analysis.print;

    auto jitter = [](const std::vector<float>& src) {
        std::vector<float> vec(src.size());
        for (std::size_t i = 0; i < src.size(); ++i) {
            vec[i] = src[i] + static_cast<float>(rng::randomDouble(-0.1, 0.1));
        }
        return vec;
    };

    drift_print_.print.mfcc = jitter(ref.mfcc);
    drift_print_.print.mel = jitter(ref.mel);
    drift_print_.print.spectral = jitter(ref.spectral);
    drift_print_.print.chroma = jitter(ref.chroma);
    drift_print_.normalised_print = drift_print_.print;
}

bool InfiniteDriftState::updateFromMatch(const AudioPrint& matched_print,
                                         const std::size_t matched_idx, const Brain& brain) {
    // Detect silent blocks and reseed.
    const double energy = [&] {
        double sum = 0.0;
        for (const float v : matched_print.mel) {
            sum += static_cast<double>(v) * static_cast<double>(v);
        }
        return sum;
    }();

    if (energy < kSilenceThreshold) {
        initFromNoise(brain);
        drift_last_idx_ = matched_idx;
        drift_stuck_count_ = 0;
        return true;
    }

    // Next target = matched fingerprint + small noise.
    copyWithNoise(drift_print_.print.mfcc, matched_print.mfcc, 0.05);
    copyWithNoise(drift_print_.print.mel, matched_print.mel, 0.05);
    copyWithNoise(drift_print_.print.spectral, matched_print.spectral, 0.05);
    drift_print_.normalised_print = drift_print_.print;

    // Stuck detection: reinit if the same block keeps winning.
    if (matched_idx == drift_last_idx_) {
        ++drift_stuck_count_;
        if (drift_stuck_count_ > kStuckThreshold) {
            initFromNoise(brain);
            drift_stuck_count_ = 0;
            return true;
        }
    } else {
        drift_stuck_count_ = 0;
        drift_last_idx_ = matched_idx;
    }

    return false;
}

void InfiniteDriftState::reset() noexcept {
    drift_print_ = {};
    drift_last_idx_ = 0;
    drift_stuck_count_ = 0;
}

}  // namespace audio
