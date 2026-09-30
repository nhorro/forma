#pragma once

#include "forma/geometry.hpp"

#include <span>

namespace forma {

/// Positive when the contour is counter-clockwise on a Y-down screen.
float signedArea(std::span<const Vec2> pts);

bool isScreenCounterClockwise(std::span<const Vec2> pts);

/// Positive when `a → b → c` turns counter-clockwise on a Y-down screen.
inline float screenCross(Vec2 a, Vec2 b, Vec2 c) { return -cross(a, b, c); }

bool pointInPolygon(Vec2 p, std::span<const Vec2> ring);

bool segmentsIntersect(Vec2 a, Vec2 b, Vec2 c, Vec2 d);

bool isConvex(std::span<const Vec2> pts);

Vec2 polygonCentroid(std::span<const Vec2> pts);

/// Monotone-chain convex hull. Output is counter-clockwise in Y-up (math) order,
/// which is clockwise on a Y-down screen. Callers that need screen winding should reverse it.
std::vector<Vec2> convexHull(std::vector<Vec2> pts);

}  // namespace forma
