#pragma once

#include <memory>

#include "../domain/BlockConfig.h"
#include "../domain/Brain.h"
#include "../domain/SearchParams.h"
#include "../domain/Sound.h"
#include "../domain/port/IBlockEffect.h"

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
 */
class SoundProcessor {
public:
    /**
     * @param params         Search / blend / effect parameters.
     * @param target_config  Block segmentation config for the target sound.
     * @param spectral_morph Optional spectral morph effect adapter.
     */
    explicit SoundProcessor(SearchParams params = {},
                            BlockConfig  target_config = {},
                            std::shared_ptr<port::IBlockEffect> spectral_morph = nullptr);

    [[nodiscard]] Sound process(Brain& brain, const Sound& target) const;

private:
    SearchParams params_;
    BlockConfig  target_config_;
    std::shared_ptr<port::IBlockEffect> spectral_morph_;
};

} // namespace audio::usecase

