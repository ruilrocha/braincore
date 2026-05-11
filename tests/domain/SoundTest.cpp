#include "domain/Sound.h"
#include "gtest/gtest.h"

#include <stdexcept>
#include <vector>

using audio::Sound;

TEST(Sound, ConstructionStoresData) {
    const std::vector<audio::Channel> channels = {{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}};
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
    std::vector<audio::Channel> channels = {{1.0, 2.0}, {3.0, 4.0}};
    Sound s(channels, 22050);

    EXPECT_DOUBLE_EQ(s.getChannel(0)[0], 1.0);
    EXPECT_DOUBLE_EQ(s.getChannel(1)[1], 4.0);
}

TEST(Sound, GetChannelThrowsOnOutOfRange) {
    Sound s({{1.0, 2.0}}, 44100);
    EXPECT_THROW((void)s.getChannel(-1), std::out_of_range);
    EXPECT_THROW((void)s.getChannel(1), std::out_of_range);
}

TEST(Sound, MoveConstructionLeavesOriginalEmpty) {
    std::vector<audio::Channel> channels = {{1.0, 2.0, 3.0}};
    Sound original(channels, 44100);
    Sound moved(std::move(original));

    EXPECT_EQ(moved.getNumSamples(), 3);
}

TEST(Sound, CopyConstructionIsIndependent) {
    std::vector<audio::Channel> channels = {{1.0, 2.0}};
    Sound a(channels, 44100);
    Sound b = a;  // NOLINT(performance-unnecessary-copy-initialization)

    EXPECT_EQ(a.getNumSamples(), b.getNumSamples());
    EXPECT_EQ(a.getSampleRate(), b.getSampleRate());
}
