#pragma once

#include "forma/geometry.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace forma {

enum class DocumentKind { Asset, World };

enum class LayerRole { Visual, Collision, Guide };

enum class PrimitiveKind { Polyline, Catmull, Polygon, Circle };

struct Canvas {
    float width = 1280.f;
    float height = 720.f;
};

struct FrameDesc {
    std::string id;
    std::string parent;
    FramePose pose{};
    bool inheritTranslation = true;
    bool inheritRotation = true;
    bool inheritScale = true;
};

struct NodeDesc {
    std::string id;
    std::string frame;
    Vec2 position{};
};

struct PrimitiveDesc {
    std::string id;
    PrimitiveKind kind = PrimitiveKind::Polyline;
    std::vector<std::string> nodes;
    std::string center;
    float radius = 1.f;
    bool closed = false;
    float curve = 1.f;
    CurveParameterization parameterization = CurveParameterization::Centripetal;
    std::optional<Color> fill;
    std::optional<StrokeStyle> stroke;
    std::string layer;
};

struct InstanceDesc {
    std::string id;
    std::string asset;
    std::string parent;
    FramePose pose{};
};

struct LayerDesc {
    std::string id;
    int order = 0;
    LayerRole role = LayerRole::Visual;
};

/// Authoring file. Positions are local to their frame. Rotation in `FramePose` is radians;
/// the JSON file stores degrees. Sampling, meshes, and bodies are not stored.
struct Document {
    int version = 1;
    DocumentKind kind = DocumentKind::World;
    std::string name;
    std::string root = "root";
    std::optional<Canvas> canvas;
    std::vector<FrameDesc> frames;
    std::vector<NodeDesc> nodes;
    std::vector<PrimitiveDesc> primitives;
    std::vector<InstanceDesc> instances;
    std::vector<LayerDesc> layers;
};

struct CompiledPrimitive {
    std::string id;
    PrimitiveKind kind = PrimitiveKind::Polyline;
    Polyline polyline{};
    CatmullRom catmull{};
    Polygon polygon{};
    Circle circle{};
    std::optional<Color> fill;
    std::optional<StrokeStyle> stroke;
    std::string layer;
};

struct CompiledDocument {
    NodePool pool;
    std::unordered_map<std::string, FrameId> frames;
    std::unordered_map<std::string, NodeId> nodes;
    std::vector<CompiledPrimitive> primitives;
};

/// `load` resolves instance asset paths. Nested instances are copied under the instance frame.
/// Grafted ids are prefixed (`mob/head`). The instance frame itself keeps its own id.
using AssetLoad = std::function<Document(std::string_view path)>;

CompiledDocument compile(const Document& document, const AssetLoad& load = {});

/// Copy the live local pose back onto matching ids. Grafted asset ids are left alone.
void writePose(Document& document, const CompiledDocument& compiled);

std::string toJson(const Document& document);
Document documentFromJson(std::string_view json);

}  // namespace forma
