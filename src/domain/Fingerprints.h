#pragma once

#include <vector>

namespace audio {

/**
 * A bundle of fingerprint vectors produced by an analyser for a single
 * audio block.
 *
 * The domain treats these as opaque numeric vectors — it does not know
 * or care whether they are MFCC coefficients, FFT bins, or something else.
 * Concrete analyser adapters decide what to put in each slot.
 *
 * Two layers exist:
 *   - **primary**: the main fingerprint used for distance computation.
 *   - **secondary**: an optional alternate representation (e.g. spectral
 *     bins vs. timbral coefficients) that can be blended with the primary.
 *
 * Each layer has a **normalised** variant computed from amplitude-normalised
 * samples, enabling amplitude-invariant matching via the `n_ratio` parameter.
 */
struct Fingerprints {
    std::vector<double> primary;              ///< Main fingerprint (raw).
    std::vector<double> secondary;            ///< Alternate fingerprint (raw).
    std::vector<double> normalised_primary;   ///< Main fingerprint (normalised).
    std::vector<double> normalised_secondary; ///< Alternate fingerprint (normalised).
    double              dominant_freq = 0.0;  ///< Dominant frequency (Hz).
};

} // namespace audio

