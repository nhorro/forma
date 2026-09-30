#pragma once

#include "forma/vec2.hpp"

#include <span>
#include <vector>

namespace forma {

/// Split a simple polygon into convex pieces of at most 8 vertices.
/// The ring is a screen-space outline. Holes are not supported.
/// Each part is screen-space counter-clockwise.
std::vector<std::vector<Vec2>> convexParts(std::span<const Vec2> ring);

}  // namespace forma
