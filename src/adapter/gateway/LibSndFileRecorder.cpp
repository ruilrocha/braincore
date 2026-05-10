#include "LibSndFileRecorder.h"

#include <iostream>

#include "sndfile.h"

namespace audio::adapter::gateway {

struct LibSndFileRecorder::Impl {
    SNDFILE* file = nullptr;
};

LibSndFileRecorder::~LibSndFileRecorder() {
    close();
}

bool LibSndFileRecorder::open(const std::string& path,
                               const int sample_rate,
                               const int channels) {
    close();

    channels_ = channels;
    impl_ = new Impl{};

    SF_INFO sfinfo{};
    sfinfo.samplerate = sample_rate;
    sfinfo.channels   = channels;
    sfinfo.format     = SF_FORMAT_WAV | SF_FORMAT_PCM_24;

    impl_->file = sf_open(path.c_str(), SFM_WRITE, &sfinfo);
    if (!impl_->file) {
        std::cerr << "LibSndFileRecorder: failed to open '" << path << "'\n";
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    return true;
}

void LibSndFileRecorder::write(const std::vector<double>& samples) {
    if (!impl_ || !impl_->file || channels_ <= 0) return;

    const auto frames = static_cast<sf_count_t>(
        samples.size() / static_cast<std::size_t>(channels_));
    if (frames > 0) {
        sf_writef_double(impl_->file, samples.data(), frames);
    }
}

void LibSndFileRecorder::close() {
    if (impl_) {
        if (impl_->file) {
            sf_close(impl_->file);
            impl_->file = nullptr;
        }
        delete impl_;
        impl_ = nullptr;
    }
    channels_ = 0;
}

bool LibSndFileRecorder::isOpen() const {
    return impl_ != nullptr && impl_->file != nullptr;
}

} // namespace audio::adapter::gateway

