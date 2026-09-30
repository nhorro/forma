#include "forma/clip.hpp"

#include <algorithm>
#include <cmath>

namespace forma {

float sampleClip(const Clip& clip, const ClipTrack& track, float time) {
    if (track.keys.empty()) {
        return 0.f;
    }
    if (track.keys.size() == 1) {
        return track.keys.front().rotation;
    }
    float local = time;
    if (clip.duration > 1e-4f) {
        local = std::fmod(time, clip.duration);
        if (local < 0.f) {
            local += clip.duration;
        }
    }
    if (local <= track.keys.front().time) {
        return track.keys.front().rotation;
    }
    if (local >= track.keys.back().time) {
        return track.keys.back().rotation;
    }
    for (std::size_t i = 1; i < track.keys.size(); ++i) {
        const ClipKey& b = track.keys[i];
        if (local > b.time) {
            continue;
        }
        const ClipKey& a = track.keys[i - 1];
        const float span = b.time - a.time;
        const float u = span <= 1e-6f ? 0.f : (local - a.time) / span;
        return a.rotation + (b.rotation - a.rotation) * u;
    }
    return track.keys.back().rotation;
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
