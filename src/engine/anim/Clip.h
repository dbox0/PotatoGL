#pragma once

#include "engine/anim/Skeleton.h"

#include <cstdint>
#include <string>
#include <vector>

// Resampled at import to a fixed rate, so playback never searches for keyframes.
// Frame-major: frames[frame * frameStride + track.offset ...], so sampling one pose reads
// two contiguous blocks. Channels that never change are stored once, in Track::constant.
struct ClipData
{
    static constexpr float DefaultRate = 30.0f;

    enum Channel : std::uint8_t
    {
        Translation = 1,
        Rotation = 2,
        Scale = 4,
    };

    struct Track
    {
        std::uint32_t jointHash = 0;
        std::uint8_t animated = 0;  // Channel bits; each animated channel takes 3, 4, 3 floats in a frame
        std::uint32_t offset = 0;   // first float of this track in a frame
        JointTrs constant;          // the channels that aren't animated
    };

    std::string name;
    float rate = DefaultRate;
    std::uint32_t frameCount = 0;
    float duration = 0.0f;  // seconds; the last frame is at duration
    bool loop = true;       // hint for new Animators
    std::vector<Track> tracks;
    std::uint32_t frameStride = 0;
    std::vector<float> frames;
};
