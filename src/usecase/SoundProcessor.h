#pragma once

#include "../domain/BlockConfig.h"
#include "../domain/Brain.h"
#include "../domain/SearchParams.h"
#include "../domain/Sound.h"

namespace audio::usecase {

/**
 * Orchestrates the "brain replacement" process.
 *
 * The target sound can be segmented with a different BlockConfig than
 * the brain's source sounds, allowing independent control of block size,
 * overlap, and window shape for ingestion vs. reconstruction.
 */
class SoundProcessor {
public:
    /**
     * @param params        Search / blend parameters.
     * @param target_config Block segmentation config for the target sound.
     *                      If not provided, the brain's own config is used.
     */
    explicit SoundProcessor(SearchParams params = {},
                            BlockConfig  target_config = {});

    /**
     * Process the target sound through the brain.
     *
     * For each channel the target is split into blocks using target_config_.
     * Channel 0 is used for fingerprint lookup; the same matched block
     * index is applied to all channels, preserving stereo coherence.
     *
     * When target_config_.overlap > 0, consecutive output blocks are
     * cross-faded using overlap-add to eliminate block-boundary clicks.
     *
     * @param brain  A populated Brain (must not be empty).
     * @param target The target sound to reconstruct.
     * @return       A new Sound with each block replaced by its best match.
     */
    [[nodiscard]] Sound process(Brain& brain, const Sound& target) const;

private:
    SearchParams params_;
    BlockConfig  target_config_;
};

} // namespace audio::usecase

