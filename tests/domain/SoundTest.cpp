#include "domain/Sound.h"
#include "gtest/gtest.h"

#include <stdexcept>
#include <vector>

using audio::Sound;

TEST(Sound, ConstructionStoresData) {
    const std::vector<audio::Channel> channels = {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}};
    Sound s(channels, 44100);

    EXPECT_EQ(s.getNumChannels(), 2);
    EXPECT_EQ(s.getNumSamples(), 3);
    EXPECT_EQ(s.getSampleRate(), 44100);
}

TEST(Sound, EmptySoundHasZeroSamples) {
    Sound s({}, 44100);
    EXPECT_EQ(s.getNumSamples(), 0);
    EXPECT_EQ(s.getNumChannels(), 0);
}

TEST(Sound, GetChannelReturnsCorrectData) {
    std::vector<audio::Channel> channels = {{1.0f, 2.0f}, {3.0f, 4.0f}};
    Sound s(channels, 22050);

    EXPECT_FLOAT_EQ(s.getChannel(0)[0], 1.0f);
    EXPECT_FLOAT_EQ(s.getChannel(1)[1], 4.0f);
}

TEST(Sound, GetChannelThrowsOnOutOfRange) {
    Sound s({{1.0f, 2.0f}}, 44100);
    EXPECT_THROW((void)s.getChannel(-1), std::out_of_range);
    EXPECT_THROW((void)s.getChannel(1), std::out_of_range);
}

TEST(Sound, MoveConstructionLeavesOriginalEmpty) {
    std::vector<audio::Channel> channels = {{1.0f, 2.0f, 3.0f}};
    Sound original(channels, 44100);
    Sound moved(std::move(original));

    EXPECT_EQ(moved.getNumSamples(), 3);
}

TEST(Sound, CopyConstructionIsIndependent) {
    std::vector<audio::Channel> channels = {{1.0f, 2.0f}};
    Sound a(channels, 44100);
    Sound b = a;  // NOLINT(performance-unnecessary-copy-initialization)

    EXPECT_EQ(a.getNumSamples(), b.getNumSamples());
    EXPECT_EQ(a.getSampleRate(), b.getSampleRate());
}

TEST(Sound, GetChannelsReturnsAllChannels) {
    std::vector<audio::Channel> channels = {{1.0f, 2.0f}, {3.0f, 4.0f}, {5.0f, 6.0f}};
    Sound s(channels, 44100);
    ASSERT_EQ(s.getChannels().size(), 3u);
    EXPECT_EQ(s.getChannels()[2][1], 6.0f);
}

TEST(Sound, GetNumChannelsMatchesInput) {
    Sound mono({audio::Channel(8, 0.0f)}, 44100);
    Sound stereo({audio::Channel(8, 0.0f), audio::Channel(8, 0.0f)}, 44100);
    EXPECT_EQ(mono.getNumChannels(), 1);
    EXPECT_EQ(stereo.getNumChannels(), 2);
}

TEST(Sound, GetSampleRateReturnsConstructedValue) {
    Sound s({audio::Channel(4, 0.0f)}, 22050);
    EXPECT_EQ(s.getSampleRate(), 22050);
}

TEST(Sound, GetNumSamplesReflectsFirstChannelLength) {
    // getNumSamples() reports the length of channel 0.
    Sound s({audio::Channel(16, 0.0f), audio::Channel(16, 0.0f)}, 44100);
    EXPECT_EQ(s.getNumSamples(), 16);
}

TEST(Sound, ZeroChannelsSoundHasZeroNumSamples) {
    Sound s({}, 44100);
    EXPECT_EQ(s.getNumSamples(), 0);
    EXPECT_EQ(s.getNumChannels(), 0);
}

TEST(Sound, GetChannelIndexZeroOnNegativeThrows) {
    Sound s({audio::Channel(4, 0.0f)}, 44100);
    EXPECT_THROW((void)s.getChannel(-1), std::out_of_range);
}

TEST(Sound, GetChannelSamplesAreCorrect) {
    std::vector<audio::Channel> channels = {{10.0f, 20.0f, 30.0f}};
    Sound s(channels, 48000);
    EXPECT_FLOAT_EQ(s.getChannel(0)[0], 10.0f);
    EXPECT_FLOAT_EQ(s.getChannel(0)[2], 30.0f);
}
