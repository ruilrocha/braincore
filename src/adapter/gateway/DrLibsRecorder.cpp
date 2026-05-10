#include "DrLibsRecorder.h"

#include <iostream>
#include <vector>

// dr_wav is already implemented in DrLibsGateway.cpp — just include the header.
#include "../../../third_party/dr_libs/dr_wav.h"

namespace audio::adapter::gateway {

struct DrLibsRecorder::Impl {
    drwav wav{};
    bool open = false;
};

DrLibsRecorder::~DrLibsRecorder() {
    close();
}

bool DrLibsRecorder::open(const std::string& path, int sample_rate, int channels) {
    close();

    impl_ = new Impl();
    channels_ = channels;

    drwav_data_format format{};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = static_cast<drwav_uint32>(channels);
    format.sampleRate = static_cast<drwav_uint32>(sample_rate);
    format.bitsPerSample = 32;

    if (!drwav_init_file_write(&impl_->wav, path.c_str(), &format, nullptr)) {
        std::cerr << "DrLibsRecorder: failed to open " << path << '\n';
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    impl_->open = true;
    return true;
}

void DrLibsRecorder::write(const std::vector<double>& samples) {
    if (!impl_ || !impl_->open) return;

    // Convert double to float for dr_wav.
    const auto frame_count = samples.size() / static_cast<std::size_t>(channels_);
    std::vector<float> buf(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        buf[i] = static_cast<float>(samples[i]);
    }

    drwav_write_pcm_frames(&impl_->wav,
                           static_cast<drwav_uint64>(frame_count), buf.data());
}

void DrLibsRecorder::close() {
    if (impl_) {
        if (impl_->open) {
            drwav_uninit(&impl_->wav);
            impl_->open = false;
        }
        delete impl_;
        impl_ = nullptr;
    }
}

bool DrLibsRecorder::isOpen() const {
    return impl_ && impl_->open;
}

} // namespace audio::adapter::gateway
