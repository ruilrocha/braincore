#pragma once

#include <vector>

namespace audio {

/**
 * A bundle of audio analysis vectors produced by an analyser for a single block.
 *
 * Four complementary fingerprint representations:
 *   - **mfcc**     (~12 coeffs): DCT of Mel log energies — very compact timbral shape.
 *   - **mel**      (~24 bins):   Raw Mel filter-bank log energies — full spectral envelope,
 *                                more detail than MFCC, less amplitude-sensitive than spectral.
 *   - **spectral** (~100 bins):  FFT magnitude — highest resolution, amplitude-sensitive.
 *   - **chroma**   (12 bins):    Pitch-class energy profile (C,C#,D,…,B), L1-normalised.
 *                                Derived from the same FFT magnitude pass — no extra FFT cost.
 *                                Captures harmonic content independent of octave and amplitude.
 *
 * `mel` is computed for free: it is an intermediate result of MFCC analysis (the output
 * of the Mel filter bank, before the DCT) and is stored rather than discarded.
 *
 * `chroma` is also computed for free: it is derived from the FFT magnitude already computed
 * for `spectral`, requiring only a single O(N/2) rebinning pass.
 *
 * Normalised variants live on the `Block::normalised_print` AudioPrint (computed from
 * DC-removed, peak-scaled samples) — not as separate fields here.
 */
struct AudioPrint {
    std::vector<double> mfcc;      ///< MFCC coefficients (~12, timbral shape).
    std::vector<double> mel;       ///< Mel filter-bank log energies (~24 bins).
    std::vector<double> spectral;  ///< FFT magnitude bins (~100, spectral detail).
    std::vector<double> chroma;    ///< Pitch-class profile (12 bins, L1-normalised).
    double dominant_freq = 0.0;    ///< Dominant frequency (Hz, spectral peak).
};

/**
 * A pair of AudioPrints for a target block: raw and amplitude-normalised.
 *
 * Passed to search strategies so they can apply n_ratio blending correctly.
 * The raw print is always required; the normalised print is only computed when
 * `SearchParams::n_ratio > 0`.
 */
struct TargetAnalysis {
    AudioPrint print;             ///< Raw fingerprint (from windowed samples).
    AudioPrint normalised_print;  ///< Amplitude-normalised fingerprint (DC-removed + peak-scaled).
};

}  // namespace audio