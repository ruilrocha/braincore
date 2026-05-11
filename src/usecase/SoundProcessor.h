#pragma once

#include <memory>

#include "../domain/BlockConfig.h"
#include "../domain/Brain.h"
#include "../domain/SearchParams.h"
#include "../domain/Sound.h"
#include "../domain/port/IBlockEffect.h"
#include "../domain/port/IVideoOutput.h"

namespace audio::usecase {

/**
 * Orchestrates the "brain replacement" process (batch mode).
 *
 * The target sound can be segmented with a different BlockConfig than
 * the brain's source sounds, allowing independent control of block size,
 * overlap, and window shape for ingestion vs. reconstruction.
 *
 * Post-processing effects (granular scatter, spectral morphing, stutter,
 * envelope shaping) are applied per-block before overlap-add.
 *
 * Optional video output: if an IVideoOutput is injected, onBlock() is called
 * for every matched block with its VideoSegment (or nullopt for audio-only).
 * The caller is responsible for calling video_output->close() after process().
 */
class SoundProcessor {
public:
    /**
     * @param params         Search / blend / effect parameters.
     * @param target_config  Block segmentation config for the target sound.
     * @param spectral_morph Optional spectral morph effect adapter.
     * @param video_output   Optional video output consumer.
     */
    explicit SoundProcessor(const SearchParams &params = {},
                            BlockConfig  target_config = {},
                            std::shared_ptr<port::IBlockEffect>  spectral_morph = nullptr,
                            std::shared_ptr<port::IVideoOutput>  video_output   = nullptr);

    [[nodiscard]] Sound process(Brain& brain, const Sound& target) const;

private:
    SearchParams params_;
    BlockConfig  target_config_;
    std::shared_ptr<port::IBlockEffect>  spectral_morph_;
    std::shared_ptr<port::IVideoOutput>  video_output_;
};

} // namespace audio::usecase

