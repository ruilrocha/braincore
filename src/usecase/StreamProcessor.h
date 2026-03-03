#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include "../domain/BlockConfig.h"
#include "../domain/Brain.h"
#include "../domain/SearchParams.h"
#include "../domain/Sound.h"
#include "../domain/port/IAudioOutput.h"
#include "../domain/port/IBlockEffect.h"

namespace audio::usecase {

/**
 * Streaming sound processor: processes and outputs audio block-by-block,
 * enabling real-time playback and infinite generative landscapes.
 *
 * Unlike SoundProcessor (which batch-processes an entire file),
 * StreamProcessor maintains internal state and yields one block at a
 * time, feeding each to an IAudioOutput for real-time playback.
 *
 * Two modes:
 *   - stream(brain, target):  process a target sound in real-time.
 *   - streamInfinite(brain):  generate audio endlessly by walking
 *                              through the brain's timbral space.
 */
class StreamProcessor {
public:
    /**
     * @param params         Search / blend / effect parameters.
     * @param target_config  How the target is segmented.
     * @param output         Audio output device (injected port).
     * @param spectral_morph Optional spectral morph effect (injected port).
     */
    StreamProcessor(SearchParams params,
                    BlockConfig  target_config,
                    std::shared_ptr<port::IAudioOutput>  output,
                    std::shared_ptr<port::IBlockEffect>  spectral_morph = nullptr);

    /**
     * Stream a target sound through the brain in real-time, looping
     * continuously until stop() is called from another thread.
     *
     * Returns false if the audio output could not be opened.
     */
    bool stream(Brain& brain, const Sound& target);

    /**
     * Generate an infinite audio landscape from the brain.
     *
     * Uses the brain's search strategy to walk through timbral space
     * without a target, producing an endless evolving soundscape.
     * Does not return until stop() is called from another thread.
     */
    void streamInfinite(Brain& brain, int sample_rate, int channels = 1);

    /**
     * Signal the streaming loop to stop (thread-safe).
     */
    void stop();

private:
    /// Process one block's effects (granular, morph, stutter, envelope).
    std::vector<double> applyEffects(const std::vector<double>& src,
                                      std::size_t block_size);

    /// Push a multi-channel block to the audio output (interleaved).
    void outputBlock(const std::vector<std::vector<double>>& channel_blocks);

    SearchParams params_;
    BlockConfig  target_config_;
    std::shared_ptr<port::IAudioOutput> output_;
    std::shared_ptr<port::IBlockEffect> spectral_morph_;

    std::vector<double> prev_block_;  ///< Previous block for spectral morph.
    std::atomic<bool>   running_{false};
};

} // namespace audio::usecase

