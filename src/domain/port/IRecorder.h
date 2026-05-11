#pragma once

#include <string>
#include <vector>

namespace audio::port {

/**
 * Port: audio output recording.
 *
 * Abstracts writing audio samples incrementally to a file so the
 * use-case layer can record without knowing about any file format
 * or library.
 *
 * Concrete implementations live in src/adapter/gateway/.
 */
class IRecorder {
public:
    virtual ~IRecorder() = default;

    /**
     * Open a file for recording.
     *
     * @param path         Output file path.
     * @param sample_rate  Recording sample rate (Hz).
     * @param channels     Number of channels.
     * @return             True if the file was opened successfully.
     */
    virtual bool open(const std::string& path, int sample_rate, int channels) = 0;

    /**
     * Write interleaved audio samples to the file.
     *
     * @param samples  Interleaved sample data (channels × frames).
     */
    virtual void write(const std::vector<double>& samples) = 0;

    /**
     * Close the file and finalize the recording.
     */
    virtual void close() = 0;

    /**
     * Check whether the recorder is currently open.
     */
    [[nodiscard]] virtual bool isOpen() const = 0;
};

}  // namespace audio::port
