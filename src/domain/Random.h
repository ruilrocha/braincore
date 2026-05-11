#pragma once

#include <cstddef>
#include <random>

/**
 * Thread-safe random number utilities using a high-quality PRNG.
 *
 * Replaces all std::rand() usage across the project with a thread-local
 * std::mt19937 engine seeded from std::random_device.
 */
namespace audio::rng {

/// Thread-local Mersenne Twister engine.
inline std::mt19937& engine() {
    thread_local std::mt19937 gen{std::random_device{}()};
    return gen;
}

/// Random double in [0.0, 1.0).
inline double randomDouble() {
    thread_local std::uniform_real_distribution dist(0.0, 1.0);
    return dist(engine());
}

/// Random double in [lo, hi).
inline double randomDouble(const double lo, const double hi) {
    std::uniform_real_distribution dist(lo, hi);
    return dist(engine());
}

/// Random index in [0, n).
inline std::size_t randomIndex(const std::size_t n) {
    if (n == 0) {
        return 0;
    }
    std::uniform_int_distribution<std::size_t> dist(0, n - 1);
    return dist(engine());
}

}  // namespace audio::rng
