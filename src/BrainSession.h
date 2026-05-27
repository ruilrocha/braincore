#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace audio {

/**
 * High-level session facade — safe for Swift C++ interop.
 *
 * Uses the Pimpl idiom: all heavy C++ headers (Brain, PlayHead, MFCC, etc.)
 * are confined to BrainSession.cpp.  This header only needs <cstddef>,
 * <memory> and <string>, so Swift's module generator never sees C++20/23
 * constructs that trigger __construct_at errors.
 *
 * ## Typical Swift usage
 * @code
 *   var session = audio.BrainSession()
 *   samples.withUnsafeBufferPointer { ptr in
 *       session.addSamples(ptr.baseAddress!, samples.count, 44100, "sine")
 *   }
 *   session.buildIndex()
 *   print(String(session.selfTest()))
 * @endcode
 */
class BrainSession {
public:
    BrainSession();
    ~BrainSession();

    // Move-only (Pimpl owns unique_ptr).
    BrainSession(BrainSession&&) noexcept;
    BrainSession& operator=(BrainSession&&) noexcept;
    BrainSession(const BrainSession&) = delete;
    BrainSession& operator=(const BrainSession&) = delete;

    // ── Ingestion ────────────────────────────────────────────────────

    /**
     * Add a mono PCM sound to the brain.
     *
     * @param samples     Raw PCM audio (double, normalised to [-1, 1]).
     * @param count       Number of samples.
     * @param sample_rate Audio sample rate in Hz.
     * @param name        Null-terminated label for diagnostics.
     */
    void addSamples(const double* samples, std::size_t count,
                    int sample_rate, const char* name);

    /**
     * Build the nearest-neighbour index.
     * Call once after all addSamples() calls, before advance().
     */
    void buildIndex();

    // ── Playback ─────────────────────────────────────────────────────

    /**
     * Advance the playhead: fingerprint @p samples and return the best-matching
     * brain block index.
     *
     * @param samples     Target block PCM (double).
     * @param count       Number of samples (should equal block_size).
     * @param sample_rate Sample rate in Hz.
     * @return            Index into the brain's block array.
     */
    std::size_t advance(const double* samples, std::size_t count, int sample_rate);

    // ── Diagnostics ──────────────────────────────────────────────────

    /// Number of blocks currently in the brain.
    [[nodiscard]] std::size_t blockCount() const noexcept;

    /// Number of samples per block (fixed at brain construction time).
    [[nodiscard]] std::size_t blockSize() const noexcept;

    /**
     * Copy the PCM samples of the block at @p index into @p out_buffer.
     *
     * This is the primary way for a Swift caller to retrieve matched audio data
     * after an advance() call.
     *
     * @param index     Block index (as returned by advance()).
     * @param out_buffer Destination buffer — must hold at least @p max_count doubles.
     * @param max_count  Maximum number of samples to write.
     * @return           Number of samples actually written (≤ max_count).
     *                   Returns 0 if index is out of range or out_buffer is null.
     */
    std::size_t getBlockSamples(std::size_t index,
                                double* out_buffer,
                                std::size_t max_count) const noexcept;

    /**
     * Quick sanity check: fingerprints block[0] and finds its nearest neighbour.
     * Returns a human-readable summary (Swift can bridge std::string directly).
     */
    [[nodiscard]] std::string selfTest();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audio
