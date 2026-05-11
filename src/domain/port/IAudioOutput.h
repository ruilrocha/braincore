#pragma once

#include <vector>

namespace audio::port {

/**
 * Port: real-time audio output.
 *
 * Abstracts the audio device so the use-case layer can push samples
 * without knowing about any specific audio API.
 *
 * Adapters live in src/adapter/playback/.
 */
class IAudioOutput {
public:
    virtual ~IAudioOutput() = default;

    /**
     * Open the audio device for playback.
     *
     * @param sample_rate  Playback sample rate (Hz).
     * @param channels     Number of output channels.
     * @param buffer_size  Preferred buffer size in frames (may be advisory).
     * @return             True if the device was opened successfully.
     */
    virtual bool open(int sample_rate, int channels, int buffer_size) = 0;

    /**
     * Push one block of interleaved audio samples to the device.
     *
     * The block is buffered internally and fed to the audio callback
     * asynchronously.  If the internal buffer is full this call may
     * block briefly until space is available.
     *
     * @param samples  Interleaved sample data (channels × frames).
     */
    virtual void write(const std::vector<double>& samples) = 0;

    /**
     * Close the audio device and release resources.
     */
    virtual void close() = 0;

    /**
     * Check whether the device is currently open and playing.
     */
    [[nodiscard]] virtual bool isOpen() const = 0;
};

}  // namespace audio::port
