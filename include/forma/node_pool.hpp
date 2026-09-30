#pragma once

#include "forma/xform.hpp"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace forma {

struct NodeId {
    uint32_t value = 0xffffffffu;

    constexpr bool valid() const { return value != 0xffffffffu; }
    friend constexpr bool operator==(NodeId a, NodeId b) = default;
};

struct FrameId {
    uint32_t value = 0xffffffffu;

    constexpr bool valid() const { return value != 0xffffffffu; }
    friend constexpr bool operator==(FrameId a, FrameId b) = default;
};

/// Local position in `frame`. `get` is local. World pixels come from `worldPosition`.
struct Node {
    Vec2 position{};
    Vec2 rest{};
    FrameId frame{};
    uint32_t generation = 0;
};

/// Stable ids, shared by every primitive that should move together.
/// Each node belongs to one frame. Frame transforms compose parent-then-local.
/// Ids are dense and are not reused; delete by rebuilding the pool from a document.
class NodePool {
public:
    NodePool();

    FrameId root() const { return root_; }
    std::size_t frameCount() const { return frames_.size(); }

    /// Parent must already exist. The new frame is identity, parented under `parent`.
    FrameId createFrame(FrameId parent);
    void reparent(FrameId frame, FrameId parent);

    void setPose(FrameId frame, const FramePose& pose);
    void placePose(FrameId frame, const FramePose& pose);
    void setInherit(FrameId frame, bool translation, bool rotation, bool scale);

    /// The pivot node lives in the parent frame. While it is set, the child's
    /// translation follows that node instead of `pose.translation`.
    void setPivot(FrameId frame, NodeId pivot);
    NodeId pivot(FrameId frame) const;

    /// Limits are radians relative to the rest rotation. `setPose` does not clamp.
    void setLimits(FrameId frame, bool enabled, float minRadians, float maxRadians);
    bool hasLimits(FrameId frame) const;
    float limitMin(FrameId frame) const;
    float limitMax(FrameId frame) const;
    void clampToLimits(FrameId frame);

    /// Skin falloff around the bone, in pixels. Zero does not deform.
    void setInfluence(FrameId frame, float radius);
    float influence(FrameId frame) const;

    /// World matrix of the bind pose. Pivot nodes contribute their rest position.
    Affine frameBind(FrameId frame) const;

    FramePose pose(FrameId frame) const;
    FramePose restPose(FrameId frame) const;
    FrameId parent(FrameId frame) const;
    uint32_t generation(FrameId frame) const;
    bool inheritsTranslation(FrameId frame) const;
    bool inheritsRotation(FrameId frame) const;
    bool inheritsScale(FrameId frame) const;

    Affine frameWorld(FrameId frame) const;
    Vec2 toLocal(FrameId frame, Vec2 world) const;

    /// On the root frame. `position` is local, which matches world while the root stays identity.
    NodeId create(Vec2 position);
    NodeId create(FrameId frame, Vec2 position);

    void set(NodeId id, Vec2 position);
    void setRest(NodeId id, Vec2 rest);
    /// Move `position` and keep `rest` in sync. Use this when the user edits a pose.
    void place(NodeId id, Vec2 position);

    Vec2 get(NodeId id) const;
    Vec2 rest(NodeId id) const;
    Vec2 worldPosition(NodeId id) const;
    FrameId frame(NodeId id) const;
    uint32_t generation(NodeId id) const;

    std::size_t size() const { return nodes_.size(); }

    void resetToRest();

private:
    struct FrameRec {
        FrameId parent{};
        FramePose pose{};
        FramePose rest{};
        bool inheritTranslation = true;
        bool inheritRotation = true;
        bool inheritScale = true;
        NodeId pivot{};
        bool limitEnabled = false;
        float limitMin = 0.f;
        float limitMax = 0.f;
        float influence = 0.f;
        uint32_t generation = 1;
    };

    struct WorldCache {
        uint32_t epoch = 0;
        Affine world{};
        Vec2 translation{};
        float rotation = 0.f;
        Vec2 scale{1.f, 1.f};
    };

    FrameRec& atFrame(FrameId id);
    const FrameRec& atFrame(FrameId id) const;
    Node& at(NodeId id);
    const Node& at(NodeId id) const;
    void touch(FrameId id);
    void touchPivots(NodeId id);
    FramePose livePose(const FrameRec& frame) const;
    const WorldCache& cached(FrameId id, int depth) const;
    static WorldCache compose(const WorldCache& parent, const FrameRec& frame);

    FrameId root_{};
    std::vector<FrameRec> frames_;
    std::vector<Node> nodes_;
    mutable uint32_t epoch_ = 1;
    mutable std::vector<WorldCache> cache_;
};

/// Changes when any referenced node moves, or when that node's frame or an ancestor does.
inline uint64_t stamp(const NodePool& pool, std::span<const NodeId> ids) {
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&](uint64_t v) {
        hash ^= v + 0x9e3779b97f4a7c15ull;
        hash *= 1099511628211ull;
    };
    std::vector<uint32_t> seen;
    seen.reserve(ids.size() * 2);
    for (NodeId id : ids) {
        mix(static_cast<uint64_t>(pool.generation(id)) ^ (static_cast<uint64_t>(id.value) << 32));
        for (FrameId frame = pool.frame(id); frame.valid(); frame = pool.parent(frame)) {
            bool already = false;
            for (uint32_t value : seen) {
                if (value == frame.value) {
                    already = true;
                    break;
                }
            }
            if (already) {
                break;
            }
            seen.push_back(frame.value);
            mix(0xC0FFEEULL ^ static_cast<uint64_t>(pool.generation(frame)) ^
                (static_cast<uint64_t>(frame.value) << 32));
        }
    }
    return hash;
}

}  // namespace forma
