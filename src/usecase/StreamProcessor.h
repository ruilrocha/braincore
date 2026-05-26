#pragma once

#include "../domain/BlockConfig.h"
#include "../domain/Brain.h"
#include "../domain/SearchParams.h"
#include "../domain/Sound.h"
#include "../domain/port/IAudioOutput.h"
#include "../domain/port/IBlockEffect.h"
#include "../domain/port/IParamController.h"
#include "../domain/port/IRecorder.h"
#include "../domain/port/ISearchStrategy.h"
#include "../domain/port/IVideoOutput.h"
#include "stages/OutputStage.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace audio::usecase {

/**
 * Streaming sound processor: processes and outputs audio block-by-block,
 * enabling real-time playback and infinite generative landscapes.
 *
 * Unlike SoundProcessor (which batch-processes an entire file),
 * StreamProcessor builds an AudioPipeline at the start of each stream/infinite
 * call.  The pipeline stages are:
 *   1. AnalysisStage  — windowed fingerprint from target block
 *   2. SearchStage    — PlayHead.advance → match
 *   3. SynthesisStage — per-channel effects + alpha blend
 *   4. OutputStage    — interleave → IAudioOutput + IRecorder + IVideoOutput
 *
 * In infinite mode the AnalysisStage is omitted; a synthetic evolving
 * fingerprint is injected directly into BlockContext.fingerprint.
 *
 * ## Thread-safety model
 *
 * The audio thread (stream / streamInfinite) exclusively owns the pipeline.
 * stop() and setRecorder() are safe to call from any thread.
 */
class StreamProcessor {
public:
    /**
     * @param brain            Brain to search through (const, immutable).
     * @param search           Search strategy to use.
     * @param params           Initial search / blend / effect parameters.
     * @param target_config    How the target is segmented.
     * @param output           Audio output device (injected port).
     * @param spectral_morph   Optional spectral morph effect (injected port).
     * @param param_controller Optional live parameter controller (injected port).
     * @param recorder         Optional output recorder (injected port).
     * @param video_output     Optional video output consumer (injected port).
     */
    StreamProcessor(std::shared_ptr<const Brain> brain,
                    std::shared_ptr<port::ISearchStrategy> search, SearchParams params,
                    BlockConfig target_config, std::shared_ptr<port::IAudioOutput> output,
                    std::shared_ptr<port::IBlockEffect> spectral_morph = nullptr,
                    std::shared_ptr<port::IParamController> param_controller = nullptr,
                    std::shared_ptr<port::IRecorder> recorder = nullptr,
                    std::shared_ptr<port::IVideoOutput> video_output = nullptr);

    bool stream(const Sound& target);
    void streamInfinite(int sample_rate, int channels = 2);

    /// Swap the recorder at runtime (thread-safe).
    void setRecorder(std::shared_ptr<port::IRecorder> recorder);

    /// Signal the streaming loop to stop (thread-safe).
    void stop();

private:
    [[nodiscard]] SearchParams activeParams() const;
    void cleanup();

    // ── Injected dependencies (immutable after construction) ───────────
    std::shared_ptr<const Brain> brain_;
    std::shared_ptr<port::ISearchStrategy> search_;
    SearchParams params_;
    BlockConfig target_config_;
    std::shared_ptr<port::IAudioOutput> output_;
    std::shared_ptr<port::IBlockEffect> spectral_morph_;
    std::shared_ptr<port::IParamController> param_controller_;
    std::shared_ptr<port::IVideoOutput> video_output_;

    // ── Recorder (swappable via setRecorder) ──────────────────────────
    std::shared_ptr<port::IRecorder> recorder_;
    mutable std::mutex recorder_mutex_;

    // ── Live pointer to OutputStage (valid during stream/streamInfinite) ─
    // Used by setRecorder() to forward the swap to the running stage.
    stages::OutputStage* output_stage_ = nullptr;

    std::atomic<bool> running_{false};
};

}  // namespace audio::usecase
