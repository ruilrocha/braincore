/**
 * @file brainio.h
 * @brief C-compatible public API for the brain-io library.
 *
 * This header exposes a minimal C API for creating and using a Brain from
 * C, Objective-C, or Swift (via bridging header or module map).
 *
 * The consumer is responsible for providing audio I/O and playback — this
 * API only exposes the analysis, brain-building, and match-finding logic.
 */

#ifndef BRAINIO_H
#define BRAINIO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Opaque handles ─────────────────────────────────────────────────────

typedef struct BrainIO_Brain* BrainIO_BrainRef;
typedef struct BrainIO_Analyser* BrainIO_AnalyserRef;

// ── Configuration ──────────────────────────────────────────────────────

typedef struct {
    int block_size;     // Samples per block (default: 4096)
    int overlap;        // Overlap in samples (default: 0)
    int window_shape;   // 0=Rect, 1=Hamming, 2=Hann, 3=Blackman, 4=Bartlett, 5=FlatTop, 6=Gaussian
} BrainIO_BlockConfig;

typedef struct {
    double alpha;
    double stickyness;
    double usage_falloff;
    double usage_weight;
    double blend_ratio;
    double n_ratio;
    int    spectral_start;
    int    spectral_end;
    double grain_size;
    double grain_scatter;
    double grain_density;
    double spectral_morph;
} BrainIO_SearchParams;

// ── Lifecycle ──────────────────────────────────────────────────────────

/**
 * Create an analyser (FFT + MFCC pipeline).
 * @param num_mfcc     Number of MFCC coefficients (default: 12).
 * @param num_fft_bins Number of FFT magnitude bins (default: 100).
 */
BrainIO_AnalyserRef brainio_analyser_create(int num_mfcc, int num_fft_bins);
void brainio_analyser_destroy(BrainIO_AnalyserRef analyser);

/**
 * Create a brain with the given configuration.
 */
BrainIO_BrainRef brainio_brain_create(
    BrainIO_AnalyserRef analyser,
    BrainIO_BlockConfig config);

void brainio_brain_destroy(BrainIO_BrainRef brain);

// ── Brain operations ───────────────────────────────────────────────────

/**
 * Add audio data to the brain.
 *
 * @param brain         Brain handle.
 * @param samples       Interleaved audio samples (float64).
 * @param num_frames    Number of audio frames.
 * @param num_channels  Number of channels.
 * @param sample_rate   Sample rate in Hz.
 * @param name          Human-readable name for this source.
 * @return              Number of blocks created.
 */
int brainio_brain_add_sound(
    BrainIO_BrainRef brain,
    const double* samples,
    size_t num_frames,
    int num_channels,
    int sample_rate,
    const char* name);

/**
 * Get the number of blocks in the brain.
 */
size_t brainio_brain_size(BrainIO_BrainRef brain);

/**
 * Build synapse graph (required for synaptic/markov search).
 */
void brainio_brain_build_synapses(BrainIO_BrainRef brain, size_t num_synapses);

// ── Match finding ──────────────────────────────────────────────────────

/**
 * Find the best matching block for a given target block.
 *
 * @param brain         Brain handle.
 * @param target_block  Mono audio block (block_size samples).
 * @param block_size    Number of samples in target_block.
 * @param sample_rate   Sample rate.
 * @param params        Search parameters.
 * @param out_samples   Output: pointer to matched block samples (caller must NOT free).
 * @param out_size      Output: number of samples in matched block.
 * @return              Index of the matched block, or -1 on error.
 */
int brainio_brain_find_match(
    BrainIO_BrainRef brain,
    const double* target_block,
    size_t block_size,
    int sample_rate,
    BrainIO_SearchParams params,
    const double** out_samples,
    size_t* out_size);

// ── Utility ────────────────────────────────────────────────────────────

/**
 * Get default search parameters.
 */
BrainIO_SearchParams brainio_default_params(void);

/**
 * Get default block configuration.
 */
BrainIO_BlockConfig brainio_default_config(void);

#ifdef __cplusplus
}
#endif

#endif // BRAINIO_H
