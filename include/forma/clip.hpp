#pragma once

#include "forma/node_pool.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace forma {

struct ClipKey {
    float time = 0.f;
    /// Radians relative to the frame's rest rotation.
    float rotation = 0.f;
};

struct ClipTrack {
    std::string frame;
    std::vector<ClipKey> keys;
};

struct Clip {
    std::string id;
    float duration = 1.f;
    std::vector<ClipTrack> tracks;
};

enum class ClipBlend { Replace, Add };

/// Loops `time` into the clip. Keys are relative to rest.
float sampleClip(const Clip& clip, const ClipTrack& track, float time);

/// Replace writes `rest + key`. Add adds `key` to the live rotation.
/// Tracks that name an unknown frame are skipped. Touched hinges are clamped.
void applyClip(NodePool& pool, const std::unordered_map<std::string, FrameId>& frames, const Clip& clip, float time,
               ClipBlend blend, float weight = 1.f);

}  // namespace forma
