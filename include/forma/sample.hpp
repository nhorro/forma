#pragma once

#include "forma/geometry.hpp"

namespace forma {

Polyline2 resolve(const NodePool& pool, const Polyline& line);
Polyline2 sample(const NodePool& pool, const CatmullRom& curve, float chordError);
Polygon2 sample(const NodePool& pool, const Circle& circle, float chordError);
Polygon2 resolve(const NodePool& pool, const Polygon& polygon);

/// Collect the node ids a primitive depends on. Useful for dirty stamps.
void collectIds(const Polyline& line, std::vector<NodeId>& out);
void collectIds(const CatmullRom& curve, std::vector<NodeId>& out);
void collectIds(const Circle& circle, std::vector<NodeId>& out);
void collectIds(const Polygon& polygon, std::vector<NodeId>& out);

}  // namespace forma
