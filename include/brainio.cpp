#include "brainio.h"

#include "../src/domain/Brain.h"
#include "../src/domain/Sound.h"
#include "../src/domain/BlockConfig.h"
#include "../src/domain/SearchParams.h"
#include "../src/domain/constants.h"
#include "../src/adapter/analysis/MfccAnalyser.h"
#include "../src/adapter/fft/PocketfftBackend.h"
#include "../src/adapter/search/ClosestSearch.h"

#include <memory>
#include <string>
#include <vector>

using namespace audio;
using namespace audio::adapter::analysis;
using namespace audio::adapter::fft;
using namespace audio::adapter::search;

// ── Internal structures ────────────────────────────────────────────────

struct BrainIO_Brain {
    std::shared_ptr<port::IAnalyser> analyser;
    std::shared_ptr<port::ISearchStrategy> search;
    Brain brain;

    BrainIO_Brain(std::shared_ptr<port::IAnalyser> a,
                  std::shared_ptr<port::ISearchStrategy> s,
                  BlockConfig cfg)
        : analyser(std::move(a))
        , search(std::move(s))
        , brain(analyser, search, cfg) {}
};

struct BrainIO_Analyser {
    std::shared_ptr<port::IFft> fft;
    std::shared_ptr<MfccAnalyser> analyser;

    BrainIO_Analyser(int num_mfcc, int num_fft_bins)
        : fft(std::make_shared<PocketfftBackend>())
        , analyser(std::make_shared<MfccAnalyser>(fft, num_mfcc, num_fft_bins)) {}
};

// ── Lifecycle ──────────────────────────────────────────────────────────

BrainIO_AnalyserRef brainio_analyser_create(int num_mfcc, int num_fft_bins) {
    return new BrainIO_Analyser(num_mfcc, num_fft_bins);
}

void brainio_analyser_destroy(BrainIO_AnalyserRef analyser) {
    delete analyser;
}

BrainIO_BrainRef brainio_brain_create(
    BrainIO_AnalyserRef analyser,
    BrainIO_BlockConfig config) {

    BlockConfig cfg;
    cfg.block_size = config.block_size;
    cfg.overlap = config.overlap;
    cfg.window = static_cast<WindowShape>(config.window_shape);

    auto search = std::make_shared<ClosestSearch>();

    return new BrainIO_Brain(analyser->analyser, search, cfg);
}

void brainio_brain_destroy(BrainIO_BrainRef brain) {
    delete brain;
}

// ── Brain operations ───────────────────────────────────────────────────

int brainio_brain_add_sound(
    BrainIO_BrainRef brain,
    const double* samples,
    size_t num_frames,
    int num_channels,
    int sample_rate,
    const char* name) {

    // Convert interleaved samples to per-channel vectors.
    std::vector<Channel> channels(num_channels);
    for (int ch = 0; ch < num_channels; ++ch) {
        channels[ch].resize(num_frames);
    }
    for (size_t f = 0; f < num_frames; ++f) {
        for (int ch = 0; ch < num_channels; ++ch) {
            channels[ch][f] = samples[f * num_channels + ch];
        }
    }

    Sound sound(std::move(channels), sample_rate);

    size_t before = brain->brain.size();
    brain->brain.addSound(sound, name ? name : "unnamed");
    size_t after = brain->brain.size();

    return static_cast<int>(after - before);
}

size_t brainio_brain_size(BrainIO_BrainRef brain) {
    return brain->brain.size();
}

void brainio_brain_build_synapses(BrainIO_BrainRef brain, size_t num_synapses) {
    brain->brain.buildSynapses(num_synapses);
}

// ── Match finding ──────────────────────────────────────────────────────

int brainio_brain_find_match(
    BrainIO_BrainRef brain,
    const double* target_block,
    size_t block_size,
    int sample_rate,
    BrainIO_SearchParams params,
    const double** out_samples,
    size_t* out_size) {

    if (!brain || !target_block || !out_samples || !out_size) return -1;

    SearchParams sp;
    sp.alpha = params.alpha;
    sp.stickyness = params.stickyness;
    sp.usage_falloff = params.usage_falloff;
    sp.usage_weight = params.usage_weight;
    sp.blend_ratio = params.blend_ratio;
    sp.n_ratio = params.n_ratio;
    sp.spectral_start = params.spectral_start;
    sp.spectral_end = params.spectral_end;
    sp.grain_size = params.grain_size;
    sp.grain_scatter = params.grain_scatter;
    sp.grain_density = params.grain_density;
    sp.spectral_morph = params.spectral_morph;

    // Create a temporary block and compute its fingerprint.
    std::vector<double> target_samples(target_block, target_block + block_size);

    auto fps = brain->analyser->analyse(target_samples, sample_rate);

    // Find best match — returns a reference to the matched block.
    const Block& match = brain->brain.findBestMatch(fps.mfcc, sp);
    *out_samples = match.samples.data();
    *out_size = match.samples.size();

    // Find the index of the match.
    const auto& all = brain->brain.blocks();
    for (size_t i = 0; i < all.size(); ++i) {
        if (&all[i] == &match) return static_cast<int>(i);
    }
    return 0;
}

// ── Utility ────────────────────────────────────────────────────────────

BrainIO_SearchParams brainio_default_params(void) {
    BrainIO_SearchParams p;
    p.alpha = 1.0;
    p.stickyness = 0.0;
    p.usage_falloff = 1.0;
    p.usage_weight = 0.0;
    p.blend_ratio = 1.0;
    p.n_ratio = 0.0;
    p.spectral_start = 0;
    p.spectral_end = 100;
    p.grain_size = 1.0;
    p.grain_scatter = 0.0;
    p.grain_density = 1.0;
    p.spectral_morph = 0.0;
    return p;
}

BrainIO_BlockConfig brainio_default_config(void) {
    BrainIO_BlockConfig c;
    c.block_size = 4096;
    c.overlap = 0;
    c.window_shape = 2; // Hann
    return c;
}
