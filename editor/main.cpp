#include "forma/document.hpp"
#include "forma/forma.hpp"
#include "forma/sfml_draw.hpp"
#include "imgui_sfml.hpp"

#include <imgui.h>

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

namespace {

constexpr float kPi = 3.14159265358979323846f;

float rad(float degrees) { return degrees * (kPi / 180.f); }
float deg(float radians) { return radians * (180.f / kPi); }

sf::Color ink() { return sf::Color(0x1c, 0x19, 0x15); }
sf::Color paper() { return sf::Color(0xe7, 0xdf, 0xd0); }
sf::Color accent() { return sf::Color(0xc4, 0x49, 0x1d); }

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("could not read " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void writeFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("could not write " + path.string());
    }
    out << text;
}

bool usedId(const forma::Document& doc, const std::string& id) {
    for (const auto& frame : doc.frames) {
        if (frame.id == id) return true;
    }
    for (const auto& node : doc.nodes) {
        if (node.id == id) return true;
    }
    for (const auto& primitive : doc.primitives) {
        if (primitive.id == id) return true;
    }
    for (const auto& instance : doc.instances) {
        if (instance.id == id) return true;
    }
    return false;
}

std::string freshId(const forma::Document& doc, const std::string& prefix) {
    for (int n = 1;; ++n) {
        const std::string id = prefix + std::to_string(n);
        if (!usedId(doc, id)) return id;
    }
}

forma::Document makeWorld() {
    forma::Document doc;
    doc.kind = forma::DocumentKind::World;
    doc.name = "world";
    doc.root = "root";
    doc.canvas = forma::Canvas{1280.f, 720.f};
    doc.frames.push_back({"root", "", {}, true, true, true, {}, false, 0.f, 0.f, 0.f});
    doc.layers.push_back({"draw", 0, forma::LayerRole::Visual});
    doc.layers.push_back({"solid", 1, forma::LayerRole::Collision});
    return doc;
}

struct Editor {
    forma::Document doc;
    forma::CompiledDocument compiled;
    std::string path = "untitled.json";
    std::string status = "Bind edits the rig. Pose bends hinges without changing the file.";
    std::string selectedFrame = "root";
    std::string selectedNode;
    std::string selectedPrimitive;
    enum class Tool { Select, Circle, Polygon, Line, Spline } tool = Tool::Select;
    enum class Drag { None, Node, Pose, Canvas, Pan, Circle } drag = Drag::None;
    std::vector<forma::Vec2> draft;
    std::string circleId;
    bool posing = false;
    forma::Vec2 cam{0.f, 0.f};
    float zoom = 1.f;
    forma::Vec2 lastMouse{};
    std::vector<forma::FrameId> ikChain;
    forma::Vec2 ikTip{};

    std::vector<forma::FrameId> childrenOf(forma::FrameId frame) const {
        std::vector<forma::FrameId> children;
        for (std::uint32_t i = 0; i < compiled.pool.frameCount(); ++i) {
            const forma::FrameId candidate{i};
            if (compiled.pool.parent(candidate) == frame) {
                children.push_back(candidate);
            }
        }
        return children;
    }

    forma::Vec2 tipOf(forma::FrameId frame) const {
        forma::Vec2 best{};
        float bestDistance = 0.f;
        for (const auto& [id, node] : compiled.nodes) {
            (void)id;
            if (compiled.pool.frame(node) != frame) {
                continue;
            }
            const forma::Vec2 local = compiled.pool.get(node);
            const float span = forma::length(local);
            if (span > bestDistance) {
                bestDistance = span;
                best = local;
            }
        }
        return best;
    }

    void prepareIk(forma::FrameId selected) {
        ikChain.clear();
        ikTip = {};
        forma::FrameId leaf = selected;
        for (;;) {
            const std::vector<forma::FrameId> children = childrenOf(leaf);
            if (children.size() != 1) {
                break;
            }
            leaf = children.front();
        }
        for (forma::FrameId cursor = leaf; cursor.valid() && cursor != compiled.pool.root();) {
            ikChain.push_back(cursor);
            const forma::FrameId parent = compiled.pool.parent(cursor);
            if (childrenOf(parent).size() != 1) {
                break;
            }
            cursor = parent;
        }
        std::reverse(ikChain.begin(), ikChain.end());
        ikTip = childrenOf(leaf).empty() ? tipOf(leaf) : forma::Vec2{};
    }
    int clipIndex = -1;
    bool playClip = false;
    float clipTime = 0.f;
    bool cameraSet = false;
    bool asset = false;
    char pathBuffer[512] = "untitled.json";

    void rebuild(bool keepPose) {
        std::unordered_map<std::string, forma::FramePose> live;
        if (keepPose) {
            for (const auto& frame : doc.frames) {
                const auto it = compiled.frames.find(frame.id);
                if (it != compiled.frames.end()) {
                    live.emplace(frame.id, compiled.pool.pose(it->second));
                }
            }
        }
        try {
            const std::filesystem::path base = std::filesystem::path(path).parent_path();
            compiled = forma::compile(doc, [&](std::string_view assetPath) {
                std::filesystem::path full{std::string(assetPath)};
                if (full.is_relative()) full = base / full;
                return forma::documentFromJson(readFile(full));
            });
            if (keepPose) {
                for (const auto& [id, pose] : live) {
                    const auto it = compiled.frames.find(id);
                    if (it == compiled.frames.end()) continue;
                    compiled.pool.setPose(it->second, pose);
                    compiled.pool.clampToLimits(it->second);
                }
            }
        } catch (const std::exception& error) {
            status = error.what();
        }
    }

    void settleCamera(const sf::RenderWindow& window) {
        if (cameraSet) return;
        const float w = static_cast<float>(window.getSize().x);
        const float h = static_cast<float>(window.getSize().y);
        if (asset || doc.kind == forma::DocumentKind::Asset) {
            cam = {-w * 0.5f, -h * 0.5f};
        }
        cameraSet = true;
    }

    forma::Vec2 screenToWorld(forma::Vec2 screen) const { return cam + screen / zoom; }

    sf::View worldView(const sf::RenderWindow& window) const {
        const float w = static_cast<float>(window.getSize().x);
        const float h = static_cast<float>(window.getSize().y);
        sf::View view;
        view.setSize({w / zoom, h / zoom});
        view.setCenter(sf::Vector2f(cam.x, cam.y) + view.getSize() * 0.5f);
        return view;
    }

    forma::FrameDesc* frameById(const std::string& id) {
        for (auto& frame : doc.frames)
            if (frame.id == id) return &frame;
        return nullptr;
    }
    forma::NodeDesc* nodeById(const std::string& id) {
        for (auto& node : doc.nodes)
            if (node.id == id) return &node;
        return nullptr;
    }
    forma::PrimitiveDesc* primitiveById(const std::string& id) {
        for (auto& primitive : doc.primitives)
            if (primitive.id == id) return &primitive;
        return nullptr;
    }

    bool locked(const std::string& id) const { return id.find('/') != std::string::npos; }

    std::string frameOfPrimitive(const forma::PrimitiveDesc& primitive) const {
        const std::string node = primitive.kind == forma::PrimitiveKind::Circle
                                     ? primitive.center
                                     : (primitive.nodes.empty() ? std::string{} : primitive.nodes.front());
        for (const auto& item : doc.nodes) {
            if (item.id == node) return item.frame;
        }
        return {};
    }

    void save() {
        try {
            writeFile(path, forma::toJson(doc));
            status = posing ? "saved the bind pose (the preview pose stays on the rig)" : "saved " + path;
        } catch (const std::exception& error) {
            status = error.what();
        }
    }

    void load(const std::string& file) {
        try {
            doc = forma::documentFromJson(readFile(file));
            path = file;
            std::snprintf(pathBuffer, sizeof(pathBuffer), "%s", path.c_str());
            asset = doc.kind == forma::DocumentKind::Asset;
            selectedFrame = doc.root;
            selectedNode.clear();
            selectedPrimitive.clear();
            draft.clear();
            posing = false;
            cameraSet = false;
            rebuild(false);
            status = "opened " + file;
        } catch (const std::exception& error) {
            status = error.what();
        }
    }

    void usePoseAsBind() {
        for (auto& frame : doc.frames) {
            const auto it = compiled.frames.find(frame.id);
            if (it == compiled.frames.end()) continue;
            const forma::FramePose live = compiled.pool.pose(it->second);
            frame.pose.rotation = live.rotation;
            if (frame.pivot.empty()) frame.pose.translation = live.translation;
        }
        posing = false;
        rebuild(false);
        status = "bind pose updated. Limits are still relative to this rest.";
    }

    void addCircle(forma::Vec2 world) {
        if (locked(selectedFrame) || !compiled.frames.contains(selectedFrame)) {
            status = "select a frame in this file";
            return;
        }
        forma::NodeDesc node;
        node.id = freshId(doc, "n");
        node.frame = selectedFrame;
        node.position = compiled.pool.toLocal(compiled.frames.at(selectedFrame), world);
        forma::PrimitiveDesc primitive;
        primitive.id = freshId(doc, "shape");
        primitive.kind = forma::PrimitiveKind::Circle;
        primitive.center = node.id;
        primitive.radius = 18.f;
        primitive.fill = forma::Color::hex(0x1e4d3c);
        primitive.stroke = forma::StrokeStyle{2.f, forma::Color::hex(0x1c1915)};
        primitive.layer = "draw";
        doc.nodes.push_back(node);
        doc.primitives.push_back(primitive);
        circleId = primitive.id;
        selectedPrimitive = primitive.id;
        selectedNode = node.id;
        drag = Drag::Circle;
        rebuild(posing);
    }

    void updateCircle(forma::Vec2 world) {
        forma::PrimitiveDesc* primitive = primitiveById(circleId);
        forma::NodeDesc* node = primitive ? nodeById(primitive->center) : nullptr;
        if (!primitive || !node || !compiled.frames.contains(node->frame)) return;
        const forma::Vec2 local = compiled.pool.toLocal(compiled.frames.at(node->frame), world);
        primitive->radius = std::max(4.f, forma::distance(node->position, local));
        rebuild(posing);
    }

    void commitDraft() {
        const int minPoints = tool == Tool::Polygon || tool == Tool::Spline ? 3 : 2;
        if (static_cast<int>(draft.size()) < minPoints) {
            status = "need " + std::to_string(minPoints) + " points";
            return;
        }
        if (locked(selectedFrame) || !compiled.frames.contains(selectedFrame)) {
            status = "select a frame in this file";
            return;
        }
        const forma::FrameId frame = compiled.frames.at(selectedFrame);
        forma::PrimitiveDesc primitive;
        primitive.id = freshId(doc, "shape");
        primitive.layer = "draw";
        primitive.stroke = forma::StrokeStyle{2.5f, forma::Color::hex(tool == Tool::Spline ? 0xc4491d : 0x1c1915)};
        if (tool == Tool::Polygon) {
            primitive.kind = forma::PrimitiveKind::Polygon;
            primitive.fill = forma::Color::hex(0x1e4d3c);
        } else if (tool == Tool::Spline) {
            primitive.kind = forma::PrimitiveKind::Catmull;
        } else {
            primitive.kind = forma::PrimitiveKind::Polyline;
        }
        for (forma::Vec2 world : draft) {
            forma::NodeDesc node;
            node.id = freshId(doc, "n");
            node.frame = selectedFrame;
            node.position = compiled.pool.toLocal(frame, world);
            primitive.nodes.push_back(node.id);
            doc.nodes.push_back(std::move(node));
        }
        selectedPrimitive = primitive.id;
        doc.primitives.push_back(std::move(primitive));
        draft.clear();
        rebuild(posing);
        status = "shape added on " + selectedFrame;
    }

    void addBone() {
        std::string parent = selectedFrame.empty() ? doc.root : selectedFrame;
        if (locked(parent) || !frameById(parent) || !compiled.frames.contains(parent)) {
            status = "select a parent frame";
            return;
        }
        forma::NodeDesc joint;
        joint.id = freshId(doc, "j");
        joint.frame = parent;
        joint.position = {48.f, 0.f};
        forma::FrameDesc child;
        child.id = freshId(doc, "bone");
        child.parent = parent;
        child.pivot = joint.id;
        child.pose.translation = joint.position;
        child.hasLimit = true;
        child.limitMin = rad(-35.f);
        child.limitMax = rad(35.f);
        doc.nodes.push_back(joint);
        doc.frames.push_back(child);
        selectedFrame = child.id;
        selectedNode = joint.id;
        rebuild(posing);
        status = "bone " + selectedFrame + " hinged at " + joint.id;
    }

    void renameFrame(const std::string& name) {
        if (name.empty() || name == selectedFrame || locked(selectedFrame) || name.find('/') != std::string::npos || usedId(doc, name)) {
            status = "name is taken";
            return;
        }
        for (auto& frame : doc.frames) {
            if (frame.id == selectedFrame) frame.id = name;
            else if (frame.parent == selectedFrame) frame.parent = name;
        }
        for (auto& node : doc.nodes)
            if (node.frame == selectedFrame) node.frame = name;
        for (auto& instance : doc.instances)
            if (instance.parent == selectedFrame) instance.parent = name;
        if (doc.root == selectedFrame) doc.root = name;
        selectedFrame = name;
        rebuild(posing);
    }

    void deleteSelection() {
        if (!selectedNode.empty() && !locked(selectedNode)) {
            const std::string id = selectedNode;
            for (auto prim = doc.primitives.begin(); prim != doc.primitives.end();) {
                if (prim->center == id) {
                    prim = doc.primitives.erase(prim);
                    continue;
                }
                std::erase(prim->nodes, id);
                const int minPoints = prim->kind == forma::PrimitiveKind::Polygon ? 3 : 2;
                if (prim->kind != forma::PrimitiveKind::Circle && static_cast<int>(prim->nodes.size()) < minPoints) {
                    prim = doc.primitives.erase(prim);
                    continue;
                }
                ++prim;
            }
            for (auto& frame : doc.frames) {
                if (frame.pivot == id) {
                    frame.pose.translation = nodeById(id) ? nodeById(id)->position : frame.pose.translation;
                    frame.pivot.clear();
                }
            }
            std::erase_if(doc.nodes, [&](const forma::NodeDesc& node) { return node.id == id; });
            selectedNode.clear();
            rebuild(posing);
            return;
        }
        if (!selectedPrimitive.empty() && !locked(selectedPrimitive)) {
            forma::PrimitiveDesc* primitive = primitiveById(selectedPrimitive);
            std::vector<std::string> owned;
            if (primitive) {
                owned = primitive->nodes;
                if (!primitive->center.empty()) owned.push_back(primitive->center);
            }
            std::erase_if(doc.primitives, [&](const forma::PrimitiveDesc& item) { return item.id == selectedPrimitive; });
            for (const std::string& id : owned) {
                bool pivot = false;
                for (const auto& frame : doc.frames)
                    if (frame.pivot == id) pivot = true;
                bool still = pivot;
                for (const auto& item : doc.primitives) {
                    if (item.center == id || std::find(item.nodes.begin(), item.nodes.end(), id) != item.nodes.end()) still = true;
                }
                if (!still) std::erase_if(doc.nodes, [&](const forma::NodeDesc& node) { return node.id == id; });
            }
            selectedPrimitive.clear();
            rebuild(posing);
            return;
        }
        if (!selectedFrame.empty() && selectedFrame != doc.root && !locked(selectedFrame) && frameById(selectedFrame)) {
            for (const auto& frame : doc.frames) {
                if (frame.parent == selectedFrame) {
                    status = "frame has children";
                    return;
                }
            }
            for (const auto& node : doc.nodes) {
                if (node.frame == selectedFrame) {
                    status = "frame has shapes or joints";
                    return;
                }
            }
            std::erase_if(doc.frames, [&](const forma::FrameDesc& frame) { return frame.id == selectedFrame; });
            selectedFrame = doc.root;
            rebuild(posing);
        }
    }

    void moveNode(forma::Vec2 world) {
        forma::NodeDesc* node = nodeById(selectedNode);
        if (!node || !compiled.frames.contains(node->frame)) return;
        node->position = compiled.pool.toLocal(compiled.frames.at(node->frame), world);
        for (auto& frame : doc.frames) {
            if (frame.pivot == node->id) frame.pose.translation = node->position;
        }
        rebuild(posing);
    }

    std::optional<std::string> hitNode(forma::Vec2 world) const {
        std::optional<std::string> hit;
        float best = 8.f / zoom;
        for (const auto& [id, node] : compiled.nodes) {
            if (locked(id)) continue;
            const float d = forma::distance(world, compiled.pool.worldPosition(node));
            if (d <= best) {
                best = d;
                hit = id;
            }
        }
        return hit;
    }

    std::optional<std::string> hitBone(forma::Vec2 world) const {
        std::optional<std::string> hit;
        float best = 8.f / zoom;
        for (const forma::Bone& bone : forma::bones(compiled.pool)) {
            const float d = forma::pointSegmentDistance(world, bone.from, bone.to);
            if (d <= best) {
                best = d;
                for (const auto& [id, frame] : compiled.frames) {
                    if (frame == bone.frame) hit = id;
                }
            }
        }
        return hit;
    }

    void onLeftDown(forma::Vec2 screen) {
        const forma::Vec2 world = screenToWorld(screen);
        if (doc.canvas) {
            const forma::Vec2 corner{doc.canvas->width, doc.canvas->height};
            if (forma::distance(world, corner) <= 10.f / zoom) {
                drag = Drag::Canvas;
                return;
            }
        }
        if (tool == Tool::Circle && !posing) {
            addCircle(world);
            return;
        }
        if (!posing && (tool == Tool::Polygon || tool == Tool::Line || tool == Tool::Spline)) {
            draft.push_back(world);
            status = "point " + std::to_string(draft.size()) + ". Finish shape when it is done.";
            return;
        }
        if (posing) {
            if (const auto bone = hitBone(world)) {
                selectedFrame = *bone;
                selectedNode.clear();
                const auto it = compiled.frames.find(selectedFrame);
                if (it != compiled.frames.end()) {
                    prepareIk(it->second);
                    drag = Editor::Drag::Pose;
                    status = "IK follows the cursor. Hinges stay inside their limits.";
                }
            }
            return;
        }
        if (const auto node = hitNode(world)) {
            selectedNode = *node;
            if (const forma::NodeDesc* desc = nodeById(*node)) selectedFrame = desc->frame;
            drag = Drag::Node;
            return;
        }
        if (const auto bone = hitBone(world)) {
            selectedFrame = *bone;
            selectedNode.clear();
            return;
        }
        selectedNode.clear();
        selectedPrimitive.clear();
    }

    void onMove(forma::Vec2 screen) {
        const forma::Vec2 world = screenToWorld(screen);
        if (drag == Drag::Pan) {
            cam += (lastMouse - screen) / zoom;
        } else if (drag == Drag::Node) {
            moveNode(world);
        } else if (drag == Drag::Canvas && doc.canvas) {
            doc.canvas->width = std::max(32.f, world.x);
            doc.canvas->height = std::max(32.f, world.y);
        } else if (drag == Drag::Circle) {
            updateCircle(world);
        } else if (drag == Drag::Pose && !ikChain.empty()) {
            try {
                forma::IkOptions options;
                const forma::IkResult solved = forma::solveIk(compiled.pool, ikChain, ikTip, world, options);
                status = solved.reached ? "IK reached the cursor" : "IK bent as far as the hinges allow";
            } catch (const std::exception& error) {
                status = error.what();
            }
        }
        lastMouse = screen;
    }

    void drawWorld(sf::RenderTarget& target) {
        if (doc.canvas) {
            sf::RectangleShape box({sf::Vector2f(doc.canvas->width, doc.canvas->height)});
            box.setFillColor(sf::Color(0xf4, 0xef, 0xe6, 210));
            box.setOutlineColor(ink());
            box.setOutlineThickness(1.f / zoom);
            target.draw(box);
            sf::RectangleShape handle({sf::Vector2f(8.f / zoom, 8.f / zoom)});
            handle.setOrigin(handle.getSize() * 0.5f);
            handle.setPosition({doc.canvas->width, doc.canvas->height});
            handle.setFillColor(accent());
            target.draw(handle);
        }
        for (const auto& primitive : compiled.primitives) {
            const bool selected = primitive.id == selectedPrimitive;
            auto strokeOf = [&](forma::StrokeStyle style) {
                if (selected) {
                    style.color = forma::Color::hex(0xc4491d);
                    style.width = std::max(style.width, 2.5f);
                }
                return style;
            };
            if (primitive.skin && primitive.kind != forma::PrimitiveKind::Circle) {
                std::vector<forma::NodeId> ids;
                bool closed = false;
                if (primitive.kind == forma::PrimitiveKind::Polyline) {
                    ids = primitive.polyline.nodes;
                    closed = primitive.polyline.closed;
                } else if (primitive.kind == forma::PrimitiveKind::Catmull) {
                    ids = primitive.catmull.nodes;
                    closed = primitive.catmull.closed;
                } else {
                    ids = primitive.polygon.nodes;
                    closed = true;
                }
                const forma::Polyline2 line = forma::deformSkinLine(compiled.pool, ids, closed);
                if (primitive.kind == forma::PrimitiveKind::Polygon && primitive.fill) {
                    forma::Polygon2 shape;
                    shape.outer.pts = line.pts;
                    forma::draw(target, forma::fillPolygon(shape, *primitive.fill));
                }
                forma::StrokeStyle style = primitive.stroke.value_or(forma::StrokeStyle{2.f, forma::Color::hex(0x1c1915)});
                forma::draw(target, forma::strokePolyline(line, strokeOf(style)));
            } else if (primitive.kind == forma::PrimitiveKind::Polyline || primitive.kind == forma::PrimitiveKind::Catmull) {
                const forma::Polyline2 line = primitive.kind == forma::PrimitiveKind::Polyline
                                                  ? forma::resolve(compiled.pool, primitive.polyline)
                                                  : forma::sample(compiled.pool, primitive.catmull, 0.6f);
                forma::StrokeStyle style = primitive.stroke.value_or(forma::StrokeStyle{2.f, forma::Color::hex(0x1c1915)});
                forma::draw(target, forma::strokePolyline(line, strokeOf(style)));
            } else if (primitive.kind == forma::PrimitiveKind::Polygon || primitive.kind == forma::PrimitiveKind::Circle) {
                const forma::Polygon2 shape = primitive.kind == forma::PrimitiveKind::Polygon
                                                  ? forma::resolve(compiled.pool, primitive.polygon)
                                                  : forma::sample(compiled.pool, primitive.circle, 0.6f);
                if (primitive.fill) forma::draw(target, forma::fillPolygon(shape, *primitive.fill));
                if (primitive.stroke || selected) {
                    forma::Polyline2 ring;
                    ring.pts = shape.outer.pts;
                    ring.closed = true;
                    forma::StrokeStyle style = primitive.stroke.value_or(forma::StrokeStyle{1.5f, forma::Color::hex(0x1c1915)});
                    forma::draw(target, forma::strokePolyline(ring, strokeOf(style)));
                }
            }
        }
        if (draft.size() >= 2) {
            forma::Polyline2 line;
            line.pts = draft;
            forma::StrokeStyle style{1.5f, forma::Color::hex(0xc4491d), forma::LineCap::Round, forma::LineJoin::Round};
            forma::draw(target, forma::strokePolyline(line, style));
        }
        for (const forma::Bone& bone : forma::bones(compiled.pool)) {
            std::string id;
            for (const auto& [name, frame] : compiled.frames)
                if (frame == bone.frame) id = name;
            const bool selected = id == selectedFrame;
            sf::VertexArray line(sf::PrimitiveType::Lines, 2);
            const sf::Color color = selected ? accent() : sf::Color(0x1c, 0x19, 0x15, 160);
            line[0] = {{bone.from.x, bone.from.y}, color};
            line[1] = {{bone.to.x, bone.to.y}, color};
            target.draw(line);
            if (selected && compiled.pool.hasLimits(bone.frame)) {
                const forma::FrameId parent = compiled.pool.parent(bone.frame);
                const forma::Affine parentWorld = compiled.pool.frameWorld(parent);
                forma::Affine linear = parentWorld;
                linear.tx = 0.f;
                linear.ty = 0.f;
                const forma::Vec2 axisX = linear.apply({1.f, 0.f});
                const forma::Vec2 axisY = linear.apply({0.f, 1.f});
                const float rest = compiled.pool.restPose(bone.frame).rotation;
                const float a0 = rest + compiled.pool.limitMin(bone.frame);
                const float a1 = rest + compiled.pool.limitMax(bone.frame);
                sf::VertexArray arc(sf::PrimitiveType::LineStrip);
                const int steps = 24;
                for (int i = 0; i <= steps; ++i) {
                    const float t = static_cast<float>(i) / static_cast<float>(steps);
                    const float angle = a0 + (a1 - a0) * t;
                    forma::Vec2 dir = axisX * std::cos(angle) + axisY * std::sin(angle);
                    const float length = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                    if (length > 1e-4f) dir = dir * ((26.f / zoom) / length);
                    arc.append({{bone.to.x + dir.x, bone.to.y + dir.y}, sf::Color(0xc4, 0x49, 0x1d, 180)});
                }
                target.draw(arc);
            }
        }
        if (!posing) {
            for (const auto& [id, node] : compiled.nodes) {
                if (locked(id)) continue;
                const forma::Vec2 p = compiled.pool.worldPosition(node);
                const float radius = (id == selectedNode ? 6.f : 4.f) / zoom;
                sf::CircleShape dot(radius);
                dot.setOrigin({radius, radius});
                dot.setPosition({p.x, p.y});
                dot.setFillColor(id == selectedNode ? accent() : sf::Color(0xf4, 0xef, 0xe6));
                dot.setOutlineColor(ink());
                dot.setOutlineThickness(1.f / zoom);
                target.draw(dot);
            }
        }
    }

    void drawUi() {
        ImGui::SetNextWindowPos({12.f, 12.f}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({360.f, 680.f}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Skeleton");
        if (ImGui::Button("Save")) save();
        ImGui::SameLine();
        if (ImGui::Button("Open")) load(pathBuffer);
        ImGui::SameLine();
        if (ImGui::Button("Tentacle")) load("examples/tentacle.json");
        ImGui::SameLine();
        if (ImGui::Button("Human")) load("examples/human.json");
        ImGui::InputText("file", pathBuffer, sizeof(pathBuffer));
        path = pathBuffer;

        if (ImGui::RadioButton("Bind", !posing)) {
            posing = false;
            compiled.pool.resetToRest();
            status = "bind pose";
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Pose", posing)) {
            posing = true;
            status = "drag a bone to bend it. Save still writes the bind pose.";
        }
        ImGui::SameLine();
        if (ImGui::Button("Use pose as bind")) usePoseAsBind();
        ImGui::TextWrapped("%s", status.c_str());
        ImGui::Separator();

        if (ImGui::TreeNodeEx("frames", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (!doc.root.empty()) {
                drawFrameTree(doc.root);
            }
            ImGui::TreePop();
        }

        forma::FrameDesc* frame = frameById(selectedFrame);
        const auto frameId = compiled.frames.find(selectedFrame);
        if (frame && frameId != compiled.frames.end()) {
            ImGui::Separator();
            ImGui::Text("frame");
            char name[128];
            std::snprintf(name, sizeof(name), "%s", selectedFrame.c_str());
            if (ImGui::InputText("name", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue)) renameFrame(name);

            if (posing) {
                float relative = deg(compiled.pool.pose(frameId->second).rotation - compiled.pool.restPose(frameId->second).rotation);
                const float lo = frame->hasLimit ? deg(frame->limitMin) : -180.f;
                const float hi = frame->hasLimit ? deg(frame->limitMax) : 180.f;
                if (ImGui::SliderFloat("angle", &relative, lo, hi, "%.1f deg")) {
                    forma::FramePose pose = compiled.pool.pose(frameId->second);
                    pose.rotation = compiled.pool.restPose(frameId->second).rotation + rad(relative);
                    compiled.pool.setPose(frameId->second, pose);
                    compiled.pool.clampToLimits(frameId->second);
                }
            } else {
                float absolute = deg(frame->pose.rotation);
                if (ImGui::SliderFloat("rest angle", &absolute, -180.f, 180.f, "%.1f deg")) {
                    frame->pose.rotation = rad(absolute);
                    rebuild(false);
                }
            }

            bool limited = frame->hasLimit;
            float lo = deg(frame->limitMin);
            float hi = deg(frame->limitMax);
            if (ImGui::Checkbox("hinge limit", &limited)) {
                frame->hasLimit = limited;
                compiled.pool.setLimits(frameId->second, limited, frame->limitMin, frame->limitMax);
                if (posing) compiled.pool.clampToLimits(frameId->second);
            }
            if (ImGui::DragFloat("min", &lo, 1.f, -180.f, 180.f, "%.0f deg") ||
                ImGui::DragFloat("max", &hi, 1.f, -180.f, 180.f, "%.0f deg")) {
                frame->hasLimit = true;
                frame->limitMin = rad(lo);
                frame->limitMax = rad(hi);
                compiled.pool.setLimits(frameId->second, true, frame->limitMin, frame->limitMax);
                if (posing) compiled.pool.clampToLimits(frameId->second);
            }
            if (ImGui::DragFloat("skin radius", &frame->influence, 0.5f, 0.f, 400.f, "%.0f")) {
                compiled.pool.setInfluence(frameId->second, frame->influence);
                status = "Skin radius deforms primitives marked skin. Parented shapes stay rigid.";
            }

            const char* pivotLabel = frame->pivot.empty() ? "(translation)" : frame->pivot.c_str();
            if (ImGui::BeginCombo("pivot", pivotLabel)) {
                if (ImGui::Selectable("(translation)", frame->pivot.empty())) {
                    frame->pivot.clear();
                    rebuild(posing);
                }
                for (const auto& node : doc.nodes) {
                    if (node.frame != frame->parent) continue;
                    if (ImGui::Selectable(node.id.c_str(), node.id == frame->pivot)) {
                        frame->pivot = node.id;
                        frame->pose.translation = node.position;
                        rebuild(posing);
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::TextDisabled("A pivot is a node in the parent. The child rotates around it.");

            if (!posing && ImGui::Button("Add child bone")) addBone();
            if (!posing && ImGui::Button("Delete")) deleteSelection();
        }

        ImGui::Separator();
        ImGui::Text("shapes on this frame");
        for (const auto& primitive : doc.primitives) {
            if (frameOfPrimitive(primitive) != selectedFrame) continue;
            if (ImGui::Selectable(primitive.id.c_str(), primitive.id == selectedPrimitive)) selectedPrimitive = primitive.id;
        }
        if (forma::PrimitiveDesc* primitive = primitiveById(selectedPrimitive)) {
            bool skin = primitive->skin;
            if (ImGui::Checkbox("skin this shape", &skin)) {
                primitive->skin = skin;
                rebuild(posing);
            }
            ImGui::TextDisabled("A skinned shape uses bone radii. It does not ride one frame.");
        }
        if (!doc.clips.empty()) {
            ImGui::Separator();
            ImGui::Text("clips");
            for (int i = 0; i < static_cast<int>(doc.clips.size()); ++i) {
                if (ImGui::Selectable(doc.clips[static_cast<std::size_t>(i)].id.c_str(), clipIndex == i)) {
                    clipIndex = i;
                    clipTime = 0.f;
                }
            }
            if (clipIndex >= 0 && clipIndex < static_cast<int>(doc.clips.size())) {
                const forma::Clip& clip = doc.clips[static_cast<std::size_t>(clipIndex)];
                if (ImGui::Checkbox("play", &playClip)) {
                    if (!playClip) {
                        compiled.pool.resetToRest();
                    }
                }
                ImGui::SliderFloat("time", &clipTime, 0.f, std::max(clip.duration, 0.01f));
                if (playClip && drag != Drag::Pose) {
                    clipTime += ImGui::GetIO().DeltaTime;
                    forma::applyClip(compiled.pool, compiled.frames, clip, clipTime, forma::ClipBlend::Replace);
                }
            }
        }
        if (!posing) {
            ImGui::Separator();
            if (ImGui::RadioButton("Select", tool == Tool::Select)) tool = Tool::Select;
            ImGui::SameLine();
            if (ImGui::RadioButton("Circle", tool == Tool::Circle)) tool = Tool::Circle;
            if (ImGui::RadioButton("Polygon", tool == Tool::Polygon)) {
                tool = Tool::Polygon;
                draft.clear();
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Line", tool == Tool::Line)) {
                tool = Tool::Line;
                draft.clear();
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Spline", tool == Tool::Spline)) {
                tool = Tool::Spline;
                draft.clear();
            }
            if (!draft.empty() && ImGui::Button("Finish shape")) commitDraft();
            ImGui::TextDisabled("Click the canvas to place. Middle mouse pans, wheel zooms.");
        } else {
            ImGui::TextDisabled("Drag a limb. The end follows the cursor, and the hinges stay inside their limits.");
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S)) save();
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete)) deleteSelection();
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Enter) && !draft.empty()) commitDraft();
        ImGui::End();
    }

    void drawFrameTree(const std::string& id) {
        std::vector<std::string> children;
        for (const auto& frame : doc.frames)
            if (frame.parent == id) children.push_back(frame.id);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
        if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
        if (id == selectedFrame) flags |= ImGuiTreeNodeFlags_Selected;
        const bool open = ImGui::TreeNodeEx(id.c_str(), flags);
        if (ImGui::IsItemClicked()) {
            selectedFrame = id;
            selectedNode.clear();
        }
        if (open) {
            for (const std::string& child : children) drawFrameTree(child);
            ImGui::TreePop();
        }
    }
};

}  // namespace

int main(int argc, char** argv) {
    bool asset = false;
    std::string file;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--asset") asset = true;
        else if (file.empty()) file = arg;
    }

    sf::RenderWindow window(sf::VideoMode({1280u, 800u}), "forma editor");
    window.setFramerateLimit(60);
    forma_ui::ImGuiLayer ui;
    if (!ui.init()) return 1;

    Editor editor;
    editor.asset = asset;
    if (!file.empty() && std::filesystem::exists(file)) {
        editor.load(file);
    } else {
        editor.doc = makeWorld();
        editor.asset = asset;
        editor.selectedFrame = editor.doc.root;
        if (!file.empty()) editor.path = file;
        std::snprintf(editor.pathBuffer, sizeof(editor.pathBuffer), "%s", editor.path.c_str());
        editor.rebuild(false);
    }

    sf::Clock clock;
    while (window.isOpen()) {
        const float dt = clock.restart().asSeconds();
        editor.settleCamera(window);
        const forma::Vec2 mouse{static_cast<float>(sf::Mouse::getPosition(window).x),
                                static_cast<float>(sf::Mouse::getPosition(window).y)};
        while (const std::optional event = window.pollEvent()) {
            ui.process(*event);
            if (event->is<sf::Event::Closed>()) window.close();
            const bool overUi = ImGui::GetIO().WantCaptureMouse;
            if (const auto* pressed = event->getIf<sf::Event::MouseButtonPressed>()) {
                editor.lastMouse = {static_cast<float>(pressed->position.x), static_cast<float>(pressed->position.y)};
                if (!overUi && (pressed->button == sf::Mouse::Button::Middle || pressed->button == sf::Mouse::Button::Right)) {
                    editor.drag = Editor::Drag::Pan;
                } else if (!overUi && pressed->button == sf::Mouse::Button::Left) {
                    editor.onLeftDown(editor.lastMouse);
                }
            } else if (const auto* released = event->getIf<sf::Event::MouseButtonReleased>()) {
                if (released->button == sf::Mouse::Button::Left || released->button == sf::Mouse::Button::Middle ||
                    released->button == sf::Mouse::Button::Right) {
                    editor.drag = Editor::Drag::None;
                    editor.circleId.clear();
                }
            } else if (event->is<sf::Event::MouseMoved>() && !overUi) {
                editor.onMove(mouse);
            } else if (const auto* wheel = event->getIf<sf::Event::MouseWheelScrolled>(); wheel && !overUi) {
                const forma::Vec2 cursor{static_cast<float>(wheel->position.x), static_cast<float>(wheel->position.y)};
                const forma::Vec2 before = editor.screenToWorld(cursor);
                editor.zoom = std::clamp(editor.zoom * (wheel->delta > 0.f ? 1.1f : 1.f / 1.1f), 0.08f, 8.f);
                editor.cam = before - cursor / editor.zoom;
            } else if (const auto* resized = event->getIf<sf::Event::Resized>()) {
                sf::View view;
                view.setSize({static_cast<float>(resized->size.x), static_cast<float>(resized->size.y)});
                view.setCenter(view.getSize() * 0.5f);
                window.setView(view);
            }
        }

        ui.begin(window, dt);
        editor.drawUi();
        window.clear(paper());
        window.setView(editor.worldView(window));
        editor.drawWorld(window);
        window.setView(window.getDefaultView());
        ui.draw(window);
        window.display();
    }
    ImGui::DestroyContext();
    return 0;
}
