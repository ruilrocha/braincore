#pragma once

#include "../AudioPrint.h"

#include <vector>

namespace audio::port {

/**
 * Port: audio-block fingerprint computation and comparison.
 *
 * This is a pure domain interface — it knows nothing about any specific
 * analysis technique.  Concrete implementations decide what the fingerprint
 * vectors actually represent.
 *
 * Adapters live in src/adapter/analysis/.
 */
class IAnalyser {
public:
    virtual ~IAnalyser() = default;

    // ── Fingerprint computation ────────────────────────────────────────

    /**
     * Compute the primary fingerprint vector for an audio block.
     *
     * @param block       Mono audio samples (already windowed if desired).
     * @param sample_rate Sample rate in Hz.
     * @return            Fingerprint vector whose semantics are adapter-defined.
     */
    [[nodiscard]] virtual std::vector<double> compute(const std::vector<double>& block,
                                                      int sample_rate) const = 0;

    /**
     * Analyse a block and produce the full AudioPrint at once.
     *
     * @param block       Mono audio samples (already windowed if desired).
     * @param sample_rate Sample rate in Hz.
     * @return            An AudioPrint bundle (mfcc, spectral, dominant freq).
     */
    [[nodiscard]] virtual AudioPrint analyse(const std::vector<double>& block,
                                             int sample_rate) const = 0;

    // ── Distance computation ───────────────────────────────────────────

    /**
     * Distance between two fingerprint vectors (lower = more similar).
     * Used for quick single-vector comparisons (e.g. stickyness check).
     */
    [[nodiscard]] virtual double distance(const std::vector<double>& a,
                                          const std::vector<double>& b) const = 0;
};

}  // namespace audio::port
