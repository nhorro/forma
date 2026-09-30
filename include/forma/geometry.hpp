#pragma once

#include "forma/node_pool.hpp"

#include <cstdint>
#include <vector>

namespace forma {

struct Segment {
    NodeId a{};
    NodeId b{};
};

struct Polyline {
    std::vector<NodeId> nodes;
    bool closed = false;
};

enum class CurveParameterization {
    Uniform,      ///< alpha = 0
    Centripetal,  ///< alpha = 0.5, the default. Avoids cusps on uneven spacing.
    Chordal,      ///< alpha = 1
};

/// Centripetal Catmull–Rom through `nodes`.
/// `curve` blends the span toward the straight chord: 1 is the spline, 0 is the polyline.
struct CatmullRom {
    std::vector<NodeId> nodes;
    CurveParameterization parameterization = CurveParameterization::Centripetal;
    float curve = 1.f;
    bool closed = false;
};

struct Circle {
    NodeId center{};
    float radius = 1.f;
};

/// Filled contour. Node order is counter-clockwise in screen space (Y down).
struct Polygon {
    std::vector<NodeId> nodes;
};

struct Polyline2 {
    std::vector<Vec2> pts;
    bool closed = false;
};

struct Contour {
    std::vector<Vec2> pts;
};

/// Outer ring is counter-clockwise in screen space. Holes are clockwise.
struct Polygon2 {
    Contour outer;
    std::vector<Contour> holes;
};

struct Color {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;

    static constexpr Color rgba(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255) {
        return Color{r_, g_, b_, a_};
    }

    static constexpr Color hex(uint32_t rgb, uint8_t a_ = 255) {
        return Color{static_cast<uint8_t>((rgb >> 16) & 255), static_cast<uint8_t>((rgb >> 8) & 255),
                     static_cast<uint8_t>(rgb & 255), a_};
    }
};

enum class LineCap { Butt, Round };
enum class LineJoin { Bevel, Round };

struct StrokeStyle {
    float width = 2.f;
    Color color = Color::hex(0xffffff);
    LineCap cap = LineCap::Round;
    LineJoin join = LineJoin::Round;
};

}  // namespace forma
