#include "forma/node_pool.hpp"

#include <algorithm>
#include <cmath>

namespace forma {
namespace {

bool samePose(const FramePose& a, const FramePose& b) {
    return a.translation == b.translation && a.rotation == b.rotation && a.scale == b.scale;
}

}  // namespace

NodePool::WorldCache NodePool::compose(const WorldCache& parent, const FrameRec& frame) {
    NodePool::WorldCache out;
    const FramePose& local = frame.pose;
    out.rotation = local.rotation + (frame.inheritRotation ? parent.rotation : 0.f);
    out.scale.x = frame.inheritScale ? parent.scale.x * local.scale.x : local.scale.x;
    out.scale.y = frame.inheritScale ? parent.scale.y * local.scale.y : local.scale.y;
    out.translation = frame.inheritTranslation ? parent.world.apply(local.translation) : local.translation;

    Affine parentLinear = parent.world;
    parentLinear.tx = 0.f;
    parentLinear.ty = 0.f;
    // Dropping a channel rebuilds the parent from the accumulated TRS, which
    // discards shear. The default path (every channel inherited) keeps the real matrix.
    if (!frame.inheritRotation || !frame.inheritScale) {
        const float rot = frame.inheritRotation ? parent.rotation : 0.f;
        const Vec2 scale = frame.inheritScale ? parent.scale : Vec2{1.f, 1.f};
        parentLinear = linearRS(rot, scale);
    }
    out.world = mul(parentLinear, linearRS(local.rotation, local.scale));
    out.world.tx = out.translation.x;
    out.world.ty = out.translation.y;
    return out;
}

NodePool::NodePool() {
    FrameRec root;
    root.pose.scale = {1.f, 1.f};
    root.rest.scale = {1.f, 1.f};
    root.generation = 1;
    frames_.push_back(root);
    root_ = FrameId{0};
}

FrameId NodePool::createFrame(FrameId parent) {
    atFrame(parent);
    FrameRec frame;
    frame.parent = parent;
    frame.pose.scale = {1.f, 1.f};
    frame.rest.scale = {1.f, 1.f};
    frames_.push_back(frame);
    ++epoch_;
    return FrameId{static_cast<uint32_t>(frames_.size() - 1)};
}

void NodePool::reparent(FrameId frame, FrameId parent) {
    if (frame == root_) {
        throw std::invalid_argument("cannot reparent the root frame");
    }
    atFrame(frame);
    atFrame(parent);
    for (FrameId cursor = parent; cursor.valid(); cursor = frames_[cursor.value].parent) {
        if (cursor == frame) {
            throw std::invalid_argument("frame cycle");
        }
    }
    FrameRec& rec = atFrame(frame);
    if (rec.parent == parent) {
        return;
    }
    rec.parent = parent;
    if (rec.pivot.valid() && this->frame(rec.pivot) != parent) {
        rec.pivot = {};
    }
    touch(frame);
}

void NodePool::setPose(FrameId id, const FramePose& pose) {
    FrameRec& frame = atFrame(id);
    if (samePose(frame.pose, pose)) {
        return;
    }
    frame.pose = pose;
    touch(id);
}

void NodePool::placePose(FrameId id, const FramePose& pose) {
    FrameRec& frame = atFrame(id);
    const bool changed = !samePose(frame.pose, pose) || !samePose(frame.rest, pose);
    frame.pose = pose;
    frame.rest = pose;
    if (changed) {
        touch(id);
    }
}

void NodePool::setInherit(FrameId id, bool translation, bool rotation, bool scale) {
    FrameRec& frame = atFrame(id);
    if (frame.inheritTranslation == translation && frame.inheritRotation == rotation && frame.inheritScale == scale) {
        return;
    }
    frame.inheritTranslation = translation;
    frame.inheritRotation = rotation;
    frame.inheritScale = scale;
    touch(id);
}

FramePose NodePool::pose(FrameId id) const { return atFrame(id).pose; }
FramePose NodePool::restPose(FrameId id) const { return atFrame(id).rest; }
FrameId NodePool::parent(FrameId id) const { return atFrame(id).parent; }
uint32_t NodePool::generation(FrameId id) const { return atFrame(id).generation; }
bool NodePool::inheritsTranslation(FrameId id) const { return atFrame(id).inheritTranslation; }
bool NodePool::inheritsRotation(FrameId id) const { return atFrame(id).inheritRotation; }
bool NodePool::inheritsScale(FrameId id) const { return atFrame(id).inheritScale; }

void NodePool::setPivot(FrameId id, NodeId pivot) {
    FrameRec& frame = atFrame(id);
    if (pivot.valid()) {
        if (id == root_) {
            throw std::invalid_argument("the root frame cannot have a pivot");
        }
        if (this->frame(pivot) != frame.parent) {
            throw std::invalid_argument("pivot node must live in the parent frame");
        }
    }
    if (frame.pivot == pivot) {
        return;
    }
    frame.pivot = pivot;
    touch(id);
}

NodeId NodePool::pivot(FrameId id) const { return atFrame(id).pivot; }

void NodePool::setLimits(FrameId id, bool enabled, float minRadians, float maxRadians) {
    if (minRadians > maxRadians) {
        std::swap(minRadians, maxRadians);
    }
    FrameRec& frame = atFrame(id);
    if (frame.limitEnabled == enabled && frame.limitMin == minRadians && frame.limitMax == maxRadians) {
        return;
    }
    frame.limitEnabled = enabled;
    frame.limitMin = minRadians;
    frame.limitMax = maxRadians;
    touch(id);
}

bool NodePool::hasLimits(FrameId id) const { return atFrame(id).limitEnabled; }
float NodePool::limitMin(FrameId id) const { return atFrame(id).limitMin; }
float NodePool::limitMax(FrameId id) const { return atFrame(id).limitMax; }

void NodePool::clampToLimits(FrameId id) {
    FrameRec& frame = atFrame(id);
    if (!frame.limitEnabled) {
        return;
    }
    const float rel = frame.pose.rotation - frame.rest.rotation;
    const float clamped = std::clamp(rel, frame.limitMin, frame.limitMax);
    if (clamped == rel) {
        return;
    }
    FramePose next = frame.pose;
    next.rotation = frame.rest.rotation + clamped;
    setPose(id, next);
}

Affine NodePool::frameBind(FrameId id) const {
    std::vector<FrameId> chain;
    for (FrameId cursor = id; cursor.valid(); cursor = atFrame(cursor).parent) {
        chain.push_back(cursor);
        if (chain.size() > 64) {
            throw std::invalid_argument("frame cycle");
        }
    }
    std::reverse(chain.begin(), chain.end());
    WorldCache acc;
    bool first = true;
    for (FrameId frameId : chain) {
        const FrameRec& frame = atFrame(frameId);
        FramePose rest = frame.rest;
        if (frame.pivot.valid()) {
            rest.translation = at(frame.pivot).rest;
        }
        if (first) {
            acc.world = trs(rest);
            acc.translation = rest.translation;
            acc.rotation = rest.rotation;
            acc.scale = rest.scale;
            first = false;
            continue;
        }
        FrameRec posed = frame;
        posed.pose = rest;
        acc = compose(acc, posed);
    }
    return acc.world;
}

Affine NodePool::frameWorld(FrameId id) const { return cached(id, 0).world; }

Vec2 NodePool::toLocal(FrameId id, Vec2 world) const {
    Affine inv;
    if (!invert(frameWorld(id), inv)) {
        return {};
    }
    return inv.apply(world);
}

NodeId NodePool::create(Vec2 position) { return create(root_, position); }

NodeId NodePool::create(FrameId frame, Vec2 position) {
    atFrame(frame);
    Node node;
    node.position = position;
    node.rest = position;
    node.generation = 1;
    node.frame = frame;
    nodes_.push_back(node);
    return NodeId{static_cast<uint32_t>(nodes_.size() - 1)};
}

void NodePool::set(NodeId id, Vec2 position) {
    Node& node = at(id);
    node.position = position;
    ++node.generation;
    touchPivots(id);
}

void NodePool::setRest(NodeId id, Vec2 rest) { at(id).rest = rest; }

void NodePool::place(NodeId id, Vec2 position) {
    Node& node = at(id);
    node.position = position;
    node.rest = position;
    ++node.generation;
    touchPivots(id);
}

Vec2 NodePool::get(NodeId id) const { return at(id).position; }
Vec2 NodePool::rest(NodeId id) const { return at(id).rest; }
FrameId NodePool::frame(NodeId id) const { return at(id).frame; }
uint32_t NodePool::generation(NodeId id) const { return at(id).generation; }

Vec2 NodePool::worldPosition(NodeId id) const {
    const Node& node = at(id);
    return cached(node.frame, 0).world.apply(node.position);
}

void NodePool::resetToRest() {
    for (FrameRec& frame : frames_) {
        if (!samePose(frame.pose, frame.rest)) {
            frame.pose = frame.rest;
            ++frame.generation;
        }
    }
    for (Node& node : nodes_) {
        if (node.position != node.rest) {
            node.position = node.rest;
            ++node.generation;
        }
    }
    ++epoch_;
}

NodePool::FrameRec& NodePool::atFrame(FrameId id) {
    if (!id.valid() || id.value >= frames_.size()) {
        throw std::out_of_range("forma::FrameId");
    }
    return frames_[id.value];
}

const NodePool::FrameRec& NodePool::atFrame(FrameId id) const {
    if (!id.valid() || id.value >= frames_.size()) {
        throw std::out_of_range("forma::FrameId");
    }
    return frames_[id.value];
}

Node& NodePool::at(NodeId id) {
    if (!id.valid() || id.value >= nodes_.size()) {
        throw std::out_of_range("forma::NodeId");
    }
    return nodes_[id.value];
}

const Node& NodePool::at(NodeId id) const {
    if (!id.valid() || id.value >= nodes_.size()) {
        throw std::out_of_range("forma::NodeId");
    }
    return nodes_[id.value];
}

void NodePool::touch(FrameId id) {
    ++atFrame(id).generation;
    ++epoch_;
}

void NodePool::touchPivots(NodeId id) {
    bool any = false;
    for (FrameRec& frame : frames_) {
        if (frame.pivot == id) {
            ++frame.generation;
            any = true;
        }
    }
    if (any) {
        ++epoch_;
    }
}

FramePose NodePool::livePose(const FrameRec& frame) const {
    FramePose pose = frame.pose;
    if (frame.pivot.valid()) {
        pose.translation = at(frame.pivot).position;
    }
    return pose;
}

const NodePool::WorldCache& NodePool::cached(FrameId id, int depth) const {
    if (depth > 64) {
        throw std::invalid_argument("frame cycle");
    }
    const FrameRec& frame = atFrame(id);
    if (cache_.size() != frames_.size()) {
        cache_.resize(frames_.size());
    }
    WorldCache& slot = cache_[id.value];
    if (slot.epoch == epoch_) {
        return slot;
    }
    FrameRec posed = frame;
    posed.pose = livePose(frame);
    WorldCache built;
    if (!frame.parent.valid()) {
        built.world = trs(posed.pose);
        built.translation = posed.pose.translation;
        built.rotation = posed.pose.rotation;
        built.scale = posed.pose.scale;
    } else {
        const WorldCache parent = cached(frame.parent, depth + 1);
        built = compose(parent, posed);
    }
    built.epoch = epoch_;
    slot = built;
    return cache_[id.value];
}

}  // namespace forma
