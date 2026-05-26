#pragma once

#include <vector>

namespace audio {

/**
 * A bundle of audio analysis vectors produced by an analyser for a single block.
 *
 * Three complementary fingerprint representations, ordered roughly by specificity:
 *   - **mfcc**     (~12 coeffs): DCT of Mel log energies — very compact timbral shape.
 *   - **mel**      (~24 bins):   Raw Mel filter-bank log energies — full spectral envelope,
 *                                more detail than MFCC, less amplitude-sensitive than spectral.
 *   - **spectral** (~100 bins):  FFT magnitude — highest resolution, amplitude-sensitive.
 *
 * `mel` is computed for free: it is an intermediate result of MFCC analysis (the output
 * of the Mel filter bank, before the DCT) and is stored rather than discarded.
 *
 * Normalised variants live on the `Block::normalised_print` AudioPrint (computed from
 * DC-removed, peak-scaled samples) — not as separate fields here.
 */
struct AudioPrint {
    std::vector<double> mfcc;      ///< MFCC coefficients (~12, timbral shape).
    std::vector<double> mel;       ///< Mel filter-bank log energies (~24 bins).
    std::vector<double> spectral;  ///< FFT magnitude bins (~100, spectral detail).
    double dominant_freq = 0.0;    ///< Dominant frequency (Hz).
};

}  // namespace audio