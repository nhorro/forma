#include "forma/clip.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace forma {
namespace {

std::vector<ClipKey> ordered(const ClipTrack& track) {
    std::vector<ClipKey> keys = track.keys;
    std::sort(keys.begin(), keys.end(), [](const ClipKey& a, const ClipKey& b) { return a.time < b.time; });
    return keys;
}

float lerp(float a, float b, float u) { return a + (b - a) * u; }

}  // namespace

float sampleClip(const Clip& clip, const ClipTrack& track, float time) {
    const std::vector<ClipKey> keys = ordered(track);
    if (keys.empty()) {
        return 0.f;
    }
    if (keys.size() == 1 || clip.duration <= 1e-4f) {
        return keys.front().rotation;
    }
    float local = std::fmod(time, clip.duration);
    if (local < 0.f) {
        local += clip.duration;
    }
    if (local < keys.front().time) {
        const float from = keys.back().time - clip.duration;
        const float span = keys.front().time - from;
        const float u = span <= 1e-6f ? 0.f : (local - from) / span;
        return lerp(keys.back().rotation, keys.front().rotation, u);
    }
    if (local >= keys.back().time) {
        const float span = clip.duration - keys.back().time;
        if (span <= 1e-6f) {
            return keys.back().rotation;
        }
        const float u = (local - keys.back().time) / span;
        return lerp(keys.back().rotation, keys.front().rotation, u);
    }
    for (std::size_t i = 1; i < keys.size(); ++i) {
        const ClipKey& b = keys[i];
        if (local > b.time) {
            continue;
        }
        const ClipKey& a = keys[i - 1];
        const float span = b.time - a.time;
        const float u = span <= 1e-6f ? 0.f : (local - a.time) / span;
        return lerp(a.rotation, b.rotation, u);
    }
    return keys.back().rotation;
}

void applyClip(NodePool& pool, const std::unordered_map<std::string, FrameId>& frames, const Clip& clip, float time,
               ClipBlend blend, float weight) {
    for (const ClipTrack& track : clip.tracks) {
        const auto found = frames.find(track.frame);
        if (found == frames.end()) {
            continue;
        }
        const float delta = sampleClip(clip, track, time) * weight;
        FramePose pose = pool.pose(found->second);
        if (blend == ClipBlend::Replace) {
            pose.rotation = pool.restPose(found->second).rotation + delta;
        } else {
            pose.rotation += delta;
        }
        pool.setPose(found->second, pose);
        pool.clampToLimits(found->second);
    }
}

}  // namespace forma
