#pragma once

#include "forma/vec2.hpp"

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

struct Node {
    Vec2 position{};
    Vec2 rest{};
    uint32_t generation = 0;
};

/// Stable ids, shared by every primitive that should move together.
/// Positions live here. Shapes store ids, never copies.
class NodePool {
public:
    NodeId create(Vec2 position) {
        Node node;
        node.position = position;
        node.rest = position;
        node.generation = 1;
        nodes_.push_back(node);
        return NodeId{static_cast<uint32_t>(nodes_.size() - 1)};
    }

    void set(NodeId id, Vec2 position) {
        Node& node = at(id);
        node.position = position;
        ++node.generation;
    }

    void setRest(NodeId id, Vec2 rest) { at(id).rest = rest; }

    /// Move `position` and keep `rest` in sync. Use this when the user edits a pose.
    void place(NodeId id, Vec2 position) {
        Node& node = at(id);
        node.position = position;
        node.rest = position;
        ++node.generation;
    }

    Vec2 get(NodeId id) const { return at(id).position; }
    Vec2 rest(NodeId id) const { return at(id).rest; }
    uint32_t generation(NodeId id) const { return at(id).generation; }

    const Node& node(NodeId id) const { return at(id); }
    std::size_t size() const { return nodes_.size(); }

private:
    Node& at(NodeId id) {
        if (id.value >= nodes_.size()) {
            throw std::out_of_range("forma::NodeId");
        }
        return nodes_[id.value];
    }
    const Node& at(NodeId id) const {
        if (id.value >= nodes_.size()) {
            throw std::out_of_range("forma::NodeId");
        }
        return nodes_[id.value];
    }

    std::vector<Node> nodes_;
};

/// Changes when any referenced node moves. Style is mixed in by the caller.
inline uint64_t stamp(const NodePool& pool, std::span<const NodeId> ids) {
    uint64_t hash = 1469598103934665603ull;
    for (NodeId id : ids) {
        const uint64_t g = pool.generation(id);
        hash ^= (g + 0x9e3779b97f4a7c15ull) ^ (static_cast<uint64_t>(id.value) << 32);
        hash *= 1099511628211ull;
    }
    return hash;
}

}  // namespace forma
