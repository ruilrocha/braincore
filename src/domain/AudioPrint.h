#pragma once

#include <vector>

namespace audio {

/**
 * A bundle of audio analysis vectors produced by an analyser for a single block.
 *
 * An AudioPrint captures two complementary representations of an audio block:
 *   - **mfcc**: Mel-Frequency Cepstral Coefficients — captures timbral shape
 *     (what the sound "sounds like" regardless of pitch/volume).
 *   - **spectral**: FFT magnitude bins — captures fine spectral detail
 *     (harmonic content, noise profile).
 *
 * Each representation has a **normalised** variant computed from
 * amplitude-normalised samples, enabling amplitude-invariant matching
 * via the `n_ratio` parameter.
 */
struct AudioPrint {
    std::vector<double> mfcc;                 ///< MFCC coefficients (timbral shape).
    std::vector<double> spectral;             ///< FFT magnitude bins (spectral detail).
    std::vector<double> normalised_mfcc;      ///< MFCC from normalised samples.
    std::vector<double> normalised_spectral;  ///< Spectral from normalised samples.
    double dominant_freq = 0.0;               ///< Dominant frequency (Hz).
};

}  // namespace audio