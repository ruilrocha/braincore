#pragma once

namespace audio {

/// Default number of samples per audio block.
constexpr int kDefaultBlockSize = 4096;

/// Default number of MFCC coefficients to compute.
constexpr int kDefaultNumMfcc = 12;

/// Default number of Mel filter bank filters.
constexpr int kDefaultMelBankSize = 24;

/// Default cross-fade alpha (1.0 = 100% source replacement).
constexpr double kDefaultAlpha = 1.0;

}  // namespace audio
