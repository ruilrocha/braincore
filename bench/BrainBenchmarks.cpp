/**
 * braincore micro-benchmarks.
 *
 * Build:
 *   conan install . --output-folder=cmake-build-debug/conan --build=missing
 *   cmake --preset conan-debug -DENABLE_BENCHMARKS=ON
 *   cmake --build --preset conan-debug --target braincore-bench
 *   ./cmake-build-debug/conan/build/Debug/braincore-bench
 *
 * The four benchmarks cover the dominant hot paths identified in
 * PERFORMANCE_AND_SCALABILITY_PLAN.md (Phase 0.1):
 *
 *   BM_BrainIngest        — Brain::addSound (FFT-per-block ingestion throughput)
 *   BM_MfccAnalyse        — MfccAnalyser::analyse (single-block latency)
 *   BM_ClosestSearch      — ClosestSearch::search at N = 1k / 10k / 100k blocks
 *   BM_BuildIndex         — Brain::buildIndex (VP-tree + K-NN precomputation)
 */

#include "adapter/analysis/MfccAnalyser.h"
#include "adapter/fft/PocketfftBackend.h"
#include "adapter/search/ClosestSearch.h"
#include "domain/Brain.h"
#include "domain/BlockConfig.h"
#include "domain/SearchContext.h"
#include "domain/SearchParams.h"
#include "domain/Sound.h"

#include <benchmark/benchmark.h>

#include <memory>
#include <vector>

namespace {

// ── Shared fixtures ────────────────────────────────────────────────────────────

std::shared_ptr<audio::port::IAnalyser> makeAnalyser() {
    auto fft = std::make_shared<audio::adapter::fft::PocketfftBackend>();
    return std::make_shared<audio::adapter::analysis::MfccAnalyser>(fft);
}

audio::Sound makeSilentMono(int samples, int sr = 44100) {
    return audio::Sound({audio::Channel(samples, 0.0f)}, sr);
}

// Build a brain with `n_blocks` blocks of `block_size` samples.
std::shared_ptr<audio::Brain> makeBrain(int n_blocks, int block_size = 4096,
                                        bool build_idx = false) {
    const audio::BlockConfig cfg{.block_size = block_size};
    auto brain = std::make_shared<audio::Brain>(makeAnalyser(), cfg);
    brain->addSound(makeSilentMono(n_blocks * block_size));
    if (build_idx) {
        brain->buildIndex(32);
    }
    return brain;
}

// ── BM_BrainIngest — ingestion throughput ─────────────────────────────────────

static void BM_BrainIngest(benchmark::State& state) {
    const int n_seconds = static_cast<int>(state.range(0));
    const int block_size = 4096;
    const int sr = 44100;
    const int n_samples = n_seconds * sr;
    const audio::BlockConfig cfg{.block_size = block_size};
    auto analyser = makeAnalyser();
    auto sound = makeSilentMono(n_samples, sr);

    for (auto _ : state) {
        audio::Brain brain(analyser, cfg);
        brain.addSound(sound);
        benchmark::DoNotOptimize(brain.size());
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                            (n_samples / block_size));
}
// Sweep corpus size: 1s, 5s, 20s of audio at 44.1 kHz.
BENCHMARK(BM_BrainIngest)->Arg(1)->Arg(5)->Arg(20)->Unit(benchmark::kMillisecond);

// ── BM_MfccAnalyse — per-block analysis latency ───────────────────────────────

static void BM_MfccAnalyse(benchmark::State& state) {
    const int block_size = 4096;
    const int sr = 44100;
    auto analyser = makeAnalyser();
    const std::vector<double> block(block_size, 0.0);

    for (auto _ : state) {
        auto fp = analyser->analyse(block, sr);
        benchmark::DoNotOptimize(fp);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MfccAnalyse)->Unit(benchmark::kMicrosecond);

// ── BM_ClosestSearch — O(N) scan latency ──────────────────────────────────────

static void BM_ClosestSearch(benchmark::State& state) {
    const int n_blocks = static_cast<int>(state.range(0));
    auto brain = makeBrain(n_blocks);

    audio::SearchParams params;
    std::vector<double> usages(brain->size(), 0.0);
    audio::SearchContext ctx{.brain = *brain,
                             .target = brain->blocks()[0].analysis,
                             .block_usages = usages,
                             .current_block_index = 0,
                             .params = params};
    audio::adapter::search::ClosestSearch strategy;

    for (auto _ : state) {
        benchmark::DoNotOptimize(strategy.search(ctx));
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * n_blocks);
}
BENCHMARK(BM_ClosestSearch)
    ->Arg(1000)
    ->Arg(5000)
    ->Arg(20000)
    ->Unit(benchmark::kMicrosecond);

// ── BM_BuildIndex — VP-tree + K-NN precomputation ─────────────────────────────

static void BM_BuildIndex(benchmark::State& state) {
    const int n_blocks = static_cast<int>(state.range(0));
    const int block_size = 4096;
    const audio::BlockConfig cfg{.block_size = block_size};
    auto analyser = makeAnalyser();
    auto sound = makeSilentMono(n_blocks * block_size);

    for (auto _ : state) {
        audio::Brain brain(analyser, cfg);
        brain.addSound(sound);
        brain.buildIndex(32);
        benchmark::DoNotOptimize(brain.hasIndex());
    }
}
BENCHMARK(BM_BuildIndex)->Arg(500)->Arg(2000)->Unit(benchmark::kMillisecond);

}  // namespace
