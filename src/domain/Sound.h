#pragma once

#include <stdexcept>
#include <utility>
#include <vector>

namespace audio {

using Channel = std::vector<double>;

/**
 * Immutable multi-channel audio container.
 *
 * num_samples and num_channels are derived from the underlying channel data
 * so they can never go out of sync.
 */
class Sound {
public:
    Sound(std::vector<Channel> channels, const int sample_rate)
        : channels_(std::move(channels)), sample_rate_(sample_rate) {}

    // Move-friendly.
    Sound(Sound&&) noexcept = default;
    Sound& operator=(Sound&&) noexcept = default;
    Sound(const Sound&) = default;
    Sound& operator=(const Sound&) = default;

    [[nodiscard]] const std::vector<Channel>& getChannels() const { return channels_; }

    /** Single channel accessor (bounds-checked). */
    [[nodiscard]] const Channel& getChannel(int index) const {
        if (index < 0 || index >= static_cast<int>(channels_.size())) {
            throw std::out_of_range("Sound::getChannel: index out of range");
        }
        return channels_[index];
    }

    [[nodiscard]] int getNumSamples() const {
        return channels_.empty() ? 0 : static_cast<int>(channels_[0].size());
    }

    [[nodiscard]] int getNumChannels() const { return static_cast<int>(channels_.size()); }

    [[nodiscard]] int getSampleRate() const { return sample_rate_; }

private:
    std::vector<Channel> channels_;
    int sample_rate_;
};

}  // namespace audio
