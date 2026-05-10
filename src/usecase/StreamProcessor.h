#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "../domain/BlockConfig.h"
#include "../domain/Brain.h"
#include "../domain/SearchParams.h"
#include "../domain/Sound.h"
#include "../domain/port/IAudioOutput.h"
#include "../domain/port/IBlockEffect.h"
#include "../domain/port/IParamController.h"
#include "../domain/port/IRecorder.h"

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
     * @param params           Initial search / blend / effect parameters.
     * @param target_config    How the target is segmented.
     * @param output           Audio output device (injected port).
     * @param spectral_morph   Optional spectral morph effect (injected port).
     * @param param_controller Optional live parameter controller (injected port).
     * @param recorder         Optional output recorder (injected port).
     */
    StreamProcessor(SearchParams params,
                    BlockConfig  target_config,
                    std::shared_ptr<port::IAudioOutput>      output,
                    std::shared_ptr<port::IBlockEffect>      spectral_morph   = nullptr,
                    std::shared_ptr<port::IParamController>  param_controller = nullptr,
                    std::shared_ptr<port::IRecorder>         recorder         = nullptr);

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
     *
     * @param brain        Brain containing fingerprinted source blocks.
     * @param sample_rate  Playback sample rate (Hz).
     * @param channels     Number of output channels (default 2 = stereo).
     */
    void streamInfinite(Brain& brain, int sample_rate, int channels = 2);

    /**
     * Set or replace the recorder at runtime (thread-safe).
     * The new recorder takes effect from the next block onward.
     */
    void setRecorder(std::shared_ptr<port::IRecorder> recorder);

    /**
     * Signal the streaming loop to stop (thread-safe).
     */
    void stop();

private:
    /// Process one block's effects (granular, morph, stutter, envelope).
    std::vector<double> applyEffects(const std::vector<double>& src,
                                      std::size_t block_size,
                                      const SearchParams& params);

    /// Push a multichannel block to the audio output (interleaved).
    /// Also tees to the recorder if one is set.
    void outputBlock(const std::vector<std::vector<double>>& channel_blocks) const;

    /// Get the active params (from controller if available, else stored copy).
    [[nodiscard]] SearchParams activeParams() const;

    SearchParams params_;
    BlockConfig  target_config_;
    std::shared_ptr<port::IAudioOutput>     output_;
    std::shared_ptr<port::IBlockEffect>     spectral_morph_;
    std::shared_ptr<port::IParamController> param_controller_;
    std::shared_ptr<port::IRecorder>        recorder_;
    mutable std::mutex                      recorder_mutex_;

    std::vector<double> prev_block_;  ///< Previous block for spectral morph.
    std::atomic<bool>   running_{false};
};

} // namespace audio::usecase

