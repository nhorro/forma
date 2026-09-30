#include "forma/document.hpp"
#include "forma/forma.hpp"
#include "forma/sfml_draw.hpp"

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace {

constexpr float kPanel = 228.f;
constexpr float kPi = 3.14159265358979323846f;

sf::Color ink() { return sf::Color(0x1c, 0x19, 0x15); }
sf::Color paper() { return sf::Color(0xe7, 0xdf, 0xd0); }
sf::Color panelColor() { return sf::Color(0xdd, 0xd4, 0xc4); }
sf::Color accent() { return sf::Color(0xc4, 0x49, 0x1d); }
sf::Color green() { return sf::Color(0x1e, 0x4d, 0x3c); }

forma::Color toForma(sf::Color color) { return forma::Color::rgba(color.r, color.g, color.b, color.a); }

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
        if (frame.id == id) {
            return true;
        }
    }
    for (const auto& node : doc.nodes) {
        if (node.id == id) {
            return true;
        }
    }
    for (const auto& primitive : doc.primitives) {
        if (primitive.id == id) {
            return true;
        }
    }
    for (const auto& instance : doc.instances) {
        if (instance.id == id) {
            return true;
        }
    }
    return false;
}

std::string freshId(const forma::Document& doc, const std::string& prefix) {
    for (int n = 1;; ++n) {
        const std::string id = prefix + std::to_string(n);
        if (!usedId(doc, id)) {
            return id;
        }
    }
}

forma::Document makeWorld() {
    forma::Document doc;
    doc.kind = forma::DocumentKind::World;
    doc.name = "world";
    doc.root = "root";
    doc.canvas = forma::Canvas{1280.f, 720.f};
    doc.frames.push_back({"root", "", {}, true, true, true});
    doc.layers.push_back({"draw", 0, forma::LayerRole::Visual});
    doc.layers.push_back({"solid", 1, forma::LayerRole::Collision});
    return doc;
}

forma::Document makeAsset() {
    forma::Document doc;
    doc.kind = forma::DocumentKind::Asset;
    doc.name = "creature";
    doc.root = "root";
    doc.frames.push_back({"root", "", {}, true, true, true});
    doc.layers.push_back({"draw", 0, forma::LayerRole::Visual});
    return doc;
}

bool loadFont(sf::Font& font) {
    const char* paths[] = {
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/Library/Fonts/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "C:/Windows/Fonts/arial.ttf",
    };
    for (const char* path : paths) {
        if (std::filesystem::exists(path) && font.openFromFile(path)) {
            return true;
        }
    }
    return false;
}

// 5x7, bit 4 is the left pixel. Lowercase is drawn as uppercase.
uint8_t glyphRow(char ch, int row) {
    char c = ch;
    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
    }
    static const uint8_t kSpace[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t kDigit[10][7] = {
        {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110},
        {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},
        {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111},
        {0b01110, 0b10001, 0b00001, 0b00110, 0b00001, 0b10001, 0b01110},
        {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010},
        {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110},
        {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110},
        {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000},
        {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110},
        {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100},
    };
    static const uint8_t kLetter[26][7] = {
        {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001},
        {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110},
        {0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110},
        {0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110},
        {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111},
        {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000},
        {0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111},
        {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001},
        {0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},
        {0b00111, 0b00010, 0b00010, 0b00010, 0b00010, 0b10010, 0b01100},
        {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001},
        {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111},
        {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001},
        {0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001},
        {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110},
        {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000},
        {0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101},
        {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001},
        {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110},
        {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100},
        {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110},
        {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100},
        {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010},
        {0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001},
        {0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100},
        {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111},
    };
    auto punct = [](char symbol, int r) -> uint8_t {
        switch (symbol) {
            case '.':
                return r == 6 ? 0b00100 : 0;
            case '-':
                return r == 3 ? 0b01110 : 0;
            case '_':
                return r == 6 ? 0b11111 : 0;
            case ':':
                return (r == 2 || r == 5) ? 0b00100 : 0;
            case '/':
                return r == 6 - 0 ? 0 : (0b10000 >> r);
            case '[':
                return r == 0 || r == 6 ? 0b01110 : 0b01000;
            case ']':
                return r == 0 || r == 6 ? 0b01110 : 0b00010;
            case '(':
                return r == 0 || r == 6 ? 0b00110 : 0b01000;
            case ')':
                return r == 0 || r == 6 ? 0b01100 : 0b00010;
            case '#':
                return (r == 2 || r == 4) ? 0b11111 : 0b01010;
            case '+':
                return r == 3 ? 0b11111 : (r > 0 && r < 6 ? 0b00100 : 0);
            default:
                return r == 0 || r == 6 ? 0b11111 : 0b10001;
        }
    };
    if (c == ' ' || c == '\0') {
        return kSpace[row];
    }
    if (c >= '0' && c <= '9') {
        return kDigit[c - '0'][row];
    }
    if (c >= 'A' && c <= 'Z') {
        return kLetter[c - 'A'][row];
    }
    return punct(c, row);
}

struct Editor {
    forma::Document doc;
    forma::CompiledDocument compiled;
    std::string path = "untitled.json";
    std::string status = "1 select   2 circle   3 polygon   4 line   5 spline   6 frame";
    std::string selectedFrame = "root";
    std::string selectedNode;
    std::string selectedPrimitive;
    enum class Tool { Select, Circle, Polygon, Line, Spline, Frame } tool = Tool::Select;
    enum class Drag { None, Node, Frame, Canvas, Pan, Circle } drag = Drag::None;
    std::vector<forma::Vec2> draft;
    std::string circleId;
    bool renaming = false;
    std::string rename;
    bool swallowText = false;
    forma::Vec2 cam{0.f, 0.f};
    float zoom = 1.f;
    forma::Vec2 lastMouse{};
    bool cameraSet = false;
    bool asset = false;

    struct Row {
        std::string id;
        float y = 0.f;
        bool instance = false;
    };
    std::vector<Row> rows;

    void rebuild() {
        try {
            const std::filesystem::path base = std::filesystem::path(path).parent_path();
            compiled = forma::compile(doc, [&](std::string_view assetPath) {
                std::filesystem::path full{std::string(assetPath)};
                if (full.is_relative()) {
                    full = base / full;
                }
                return forma::documentFromJson(readFile(full));
            });
        } catch (const std::exception& error) {
            status = error.what();
        }
    }

    float viewW(const sf::RenderWindow& window) const { return std::max(1.f, static_cast<float>(window.getSize().x) - kPanel); }
    float viewH(const sf::RenderWindow& window) const { return std::max(1.f, static_cast<float>(window.getSize().y) - 28.f); }

    void settleCamera(const sf::RenderWindow& window) {
        if (cameraSet) {
            return;
        }
        if (asset) {
            cam = {-viewW(window) * 0.5f, -viewH(window) * 0.5f};
        }
        cameraSet = true;
    }

    forma::Vec2 screenToWorld(forma::Vec2 screen) const {
        return cam + (screen - forma::Vec2{kPanel, 0.f}) / zoom;
    }

    sf::View worldView(const sf::RenderWindow& window) const {
        const float w = viewW(window);
        const float h = viewH(window);
        const float winW = std::max(1.f, static_cast<float>(window.getSize().x));
        const float winH = std::max(1.f, static_cast<float>(window.getSize().y));
        sf::View view;
        view.setViewport(sf::FloatRect({kPanel / winW, 0.f}, {w / winW, h / winH}));
        view.setSize({w / zoom, h / zoom});
        view.setCenter(sf::Vector2f(cam.x, cam.y) + view.getSize() * 0.5f);
        return view;
    }

    forma::FrameDesc* frameById(const std::string& id) {
        for (auto& frame : doc.frames) {
            if (frame.id == id) {
                return &frame;
            }
        }
        return nullptr;
    }

    forma::NodeDesc* nodeById(const std::string& id) {
        for (auto& node : doc.nodes) {
            if (node.id == id) {
                return &node;
            }
        }
        return nullptr;
    }

    forma::PrimitiveDesc* primitiveById(const std::string& id) {
        for (auto& primitive : doc.primitives) {
            if (primitive.id == id) {
                return &primitive;
            }
        }
        return nullptr;
    }

    forma::InstanceDesc* instanceById(const std::string& id) {
        for (auto& instance : doc.instances) {
            if (instance.id == id) {
                return &instance;
            }
        }
        return nullptr;
    }

    bool locked(const std::string& id) const { return id.find('/') != std::string::npos; }

    std::string frameOfSelection() const {
        if (!selectedFrame.empty()) {
            return selectedFrame;
        }
        return doc.root;
    }

    void save() {
        try {
            writeFile(path, forma::toJson(doc));
            status = "saved " + path;
        } catch (const std::exception& error) {
            status = error.what();
        }
    }

    void load(const std::string& file) {
        try {
            doc = forma::documentFromJson(readFile(file));
            path = file;
            asset = doc.kind == forma::DocumentKind::Asset;
            selectedFrame = doc.root;
            selectedNode.clear();
            selectedPrimitive.clear();
            draft.clear();
            renaming = false;
            cameraSet = false;
            rebuild();
            status = "opened " + file;
        } catch (const std::exception& error) {
            status = error.what();
        }
    }

    void addCircle(forma::Vec2 world) {
        const std::string frameName = frameOfSelection();
        if (locked(frameName) || !compiled.frames.contains(frameName)) {
            status = "select a frame in this file";
            return;
        }
        forma::NodeDesc node;
        node.id = freshId(doc, "n");
        node.frame = frameName;
        node.position = compiled.pool.toLocal(compiled.frames.at(frameName), world);
        forma::PrimitiveDesc primitive;
        primitive.id = freshId(doc, "shape");
        primitive.kind = forma::PrimitiveKind::Circle;
        primitive.center = node.id;
        primitive.radius = 8.f;
        primitive.fill = forma::Color::hex(0x1e4d3c);
        primitive.stroke = forma::StrokeStyle{2.f, forma::Color::hex(0x1c1915)};
        primitive.layer = "draw";
        doc.nodes.push_back(node);
        doc.primitives.push_back(primitive);
        circleId = primitive.id;
        selectedPrimitive = primitive.id;
        selectedNode = node.id;
        selectedFrame = frameName;
        drag = Drag::Circle;
        rebuild();
    }

    void updateCircle(forma::Vec2 world) {
        forma::PrimitiveDesc* primitive = primitiveById(circleId);
        if (!primitive) {
            return;
        }
        forma::NodeDesc* node = nodeById(primitive->center);
        if (!node || !compiled.frames.contains(node->frame)) {
            return;
        }
        const forma::Vec2 local = compiled.pool.toLocal(compiled.frames.at(node->frame), world);
        primitive->radius = std::max(4.f, forma::distance(node->position, local));
        rebuild();
    }

    void addDraftPoint(forma::Vec2 world) {
        draft.push_back(world);
        status = "point " + std::to_string(draft.size()) + "   enter finishes, esc cancels";
    }

    void commitDraft() {
        const int minPoints = tool == Tool::Polygon || tool == Tool::Spline ? 3 : 2;
        if (static_cast<int>(draft.size()) < minPoints) {
            status = "need " + std::to_string(minPoints) + " points";
            return;
        }
        const std::string frameName = frameOfSelection();
        if (locked(frameName) || !compiled.frames.contains(frameName)) {
            status = "select a frame in this file";
            return;
        }
        const forma::FrameId frame = compiled.frames.at(frameName);
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
            node.frame = frameName;
            node.position = compiled.pool.toLocal(frame, world);
            primitive.nodes.push_back(node.id);
            doc.nodes.push_back(std::move(node));
        }
        selectedPrimitive = primitive.id;
        selectedFrame = frameName;
        doc.primitives.push_back(std::move(primitive));
        draft.clear();
        rebuild();
        status = "shape added";
    }

    void addFrame(forma::Vec2 world) {
        std::string parent = frameOfSelection();
        if (locked(parent) || !frameById(parent)) {
            parent = doc.root;
        }
        if (!compiled.frames.contains(parent)) {
            status = "no parent frame";
            return;
        }
        forma::FrameDesc frame;
        frame.id = freshId(doc, "frame");
        frame.parent = parent;
        frame.pose.translation = compiled.pool.toLocal(compiled.frames.at(parent), world);
        selectedFrame = frame.id;
        selectedNode.clear();
        selectedPrimitive.clear();
        doc.frames.push_back(frame);
        rebuild();
        status = "frame " + selectedFrame;
    }

    void applyRename() {
        if (!renaming) {
            return;
        }
        renaming = false;
        if (rename.empty() || rename == selectedFrame || locked(selectedFrame)) {
            return;
        }
        if (rename.find('/') != std::string::npos || usedId(doc, rename)) {
            status = "name is taken";
            return;
        }
        for (auto& frame : doc.frames) {
            if (frame.id == selectedFrame) {
                frame.id = rename;
            } else if (frame.parent == selectedFrame) {
                frame.parent = rename;
            }
        }
        for (auto& node : doc.nodes) {
            if (node.frame == selectedFrame) {
                node.frame = rename;
            }
        }
        for (auto& instance : doc.instances) {
            if (instance.parent == selectedFrame) {
                instance.parent = rename;
            }
        }
        if (doc.root == selectedFrame) {
            doc.root = rename;
        }
        selectedFrame = rename;
        rebuild();
        status = "renamed " + selectedFrame;
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
            std::erase_if(doc.nodes, [&](const forma::NodeDesc& node) { return node.id == id; });
            selectedNode.clear();
            selectedPrimitive.clear();
            rebuild();
            return;
        }
        if (!selectedPrimitive.empty() && !locked(selectedPrimitive)) {
            forma::PrimitiveDesc* primitive = primitiveById(selectedPrimitive);
            std::vector<std::string> owned;
            if (primitive) {
                owned = primitive->nodes;
                if (!primitive->center.empty()) {
                    owned.push_back(primitive->center);
                }
            }
            std::erase_if(doc.primitives, [&](const forma::PrimitiveDesc& item) { return item.id == selectedPrimitive; });
            for (const std::string& id : owned) {
                bool still = false;
                for (const auto& item : doc.primitives) {
                    if (item.center == id || std::find(item.nodes.begin(), item.nodes.end(), id) != item.nodes.end()) {
                        still = true;
                    }
                }
                if (!still) {
                    std::erase_if(doc.nodes, [&](const forma::NodeDesc& node) { return node.id == id; });
                }
            }
            selectedPrimitive.clear();
            selectedNode.clear();
            rebuild();
            return;
        }
        if (!selectedFrame.empty() && selectedFrame != doc.root && !locked(selectedFrame)) {
            for (const auto& frame : doc.frames) {
                if (frame.parent == selectedFrame) {
                    status = "frame has children";
                    return;
                }
            }
            for (const auto& node : doc.nodes) {
                if (node.frame == selectedFrame) {
                    status = "frame has shapes";
                    return;
                }
            }
            std::erase_if(doc.frames, [&](const forma::FrameDesc& frame) { return frame.id == selectedFrame; });
            selectedFrame = doc.root;
            rebuild();
        }
    }

    void toggleLayer() {
        forma::PrimitiveDesc* primitive = primitiveById(selectedPrimitive);
        if (!primitive || locked(selectedPrimitive)) {
            return;
        }
        primitive->layer = primitive->layer == "solid" ? "draw" : "solid";
        status = primitive->id + " layer " + primitive->layer;
    }

    void moveNode(forma::Vec2 world) {
        forma::NodeDesc* node = nodeById(selectedNode);
        if (!node || !compiled.frames.contains(node->frame)) {
            return;
        }
        node->position = compiled.pool.toLocal(compiled.frames.at(node->frame), world);
        rebuild();
    }

    void moveFrame(forma::Vec2 world) {
        if (forma::InstanceDesc* instance = instanceById(selectedFrame)) {
            const std::string parent = instance->parent.empty() ? doc.root : instance->parent;
            if (!compiled.frames.contains(parent)) {
                return;
            }
            instance->pose.translation = compiled.pool.toLocal(compiled.frames.at(parent), world);
            rebuild();
            return;
        }
        forma::FrameDesc* frame = frameById(selectedFrame);
        if (!frame) {
            return;
        }
        if (frame->parent.empty()) {
            frame->pose.translation = world;
        } else if (compiled.frames.contains(frame->parent)) {
            frame->pose.translation = compiled.pool.toLocal(compiled.frames.at(frame->parent), world);
        }
        rebuild();
    }

    std::optional<std::string> hitNode(forma::Vec2 world) const {
        std::optional<std::string> hit;
        float best = 8.f / zoom;
        for (const auto& [id, node] : compiled.nodes) {
            if (locked(id)) {
                continue;
            }
            const float d = forma::distance(world, compiled.pool.worldPosition(node));
            if (d <= best) {
                best = d;
                hit = id;
            }
        }
        return hit;
    }

    std::optional<std::string> hitFrame(forma::Vec2 world) const {
        std::optional<std::string> hit;
        float best = 9.f / zoom;
        for (const auto& frame : doc.frames) {
            const auto it = compiled.frames.find(frame.id);
            if (it == compiled.frames.end()) {
                continue;
            }
            const forma::Vec2 origin = compiled.pool.frameWorld(it->second).apply({0.f, 0.f});
            const float d = forma::distance(world, origin);
            if (d <= best) {
                best = d;
                hit = frame.id;
            }
        }
        for (const auto& instance : doc.instances) {
            const auto it = compiled.frames.find(instance.id);
            if (it == compiled.frames.end()) {
                continue;
            }
            const forma::Vec2 origin = compiled.pool.frameWorld(it->second).apply({0.f, 0.f});
            const float d = forma::distance(world, origin);
            if (d <= best) {
                best = d;
                hit = instance.id;
            }
        }
        return hit;
    }

    std::optional<std::string> hitPrimitive(forma::Vec2 world) const {
        std::optional<std::string> hit;
        float best = 7.f / zoom;
        for (const auto& primitive : compiled.primitives) {
            forma::Polyline2 line;
            forma::Polygon2 fill;
            bool filled = false;
            if (primitive.kind == forma::PrimitiveKind::Polyline) {
                line = forma::resolve(compiled.pool, primitive.polyline);
            } else if (primitive.kind == forma::PrimitiveKind::Catmull) {
                line = forma::sample(compiled.pool, primitive.catmull, 0.8f);
            } else if (primitive.kind == forma::PrimitiveKind::Polygon) {
                fill = forma::resolve(compiled.pool, primitive.polygon);
                line.pts = fill.outer.pts;
                line.closed = true;
                filled = primitive.fill.has_value();
            } else {
                fill = forma::sample(compiled.pool, primitive.circle, 0.8f);
                line.pts = fill.outer.pts;
                line.closed = true;
                filled = primitive.fill.has_value();
            }
            if (filled && forma::pointInPolygon(world, fill.outer.pts)) {
                return primitive.id;
            }
            const std::size_t count = line.pts.size();
            const std::size_t segments = line.closed ? count : (count == 0 ? 0 : count - 1);
            for (std::size_t i = 0; i < segments; ++i) {
                const float d = forma::pointSegmentDistance(world, line.pts[i], line.pts[(i + 1) % count]);
                if (d <= best) {
                    best = d;
                    hit = primitive.id;
                }
            }
        }
        return hit;
    }

    void onLeftDown(forma::Vec2 screen) {
        if (screen.x < kPanel) {
            for (const Row& row : rows) {
                if (std::fabs(screen.y - row.y) <= 9.f) {
                    selectedFrame = row.id;
                    selectedNode.clear();
                    selectedPrimitive.clear();
                    status = (row.instance ? "instance " : "frame ") + row.id;
                    return;
                }
            }
            if (screen.y > 250.f && screen.y < 274.f) {
                doc.kind = doc.kind == forma::DocumentKind::Asset ? forma::DocumentKind::World : forma::DocumentKind::Asset;
                asset = doc.kind == forma::DocumentKind::Asset;
                if (doc.kind == forma::DocumentKind::World && !doc.canvas) {
                    doc.canvas = forma::Canvas{1280.f, 720.f};
                }
                status = doc.kind == forma::DocumentKind::Asset ? "asset" : "world";
                return;
            }
            return;
        }
        const forma::Vec2 world = screenToWorld(screen);
        if (doc.canvas) {
            const forma::Vec2 corner{doc.canvas->width, doc.canvas->height};
            if (forma::distance(world, corner) <= 10.f / zoom) {
                drag = Drag::Canvas;
                return;
            }
        }
        if (tool == Tool::Circle) {
            addCircle(world);
            return;
        }
        if (tool == Tool::Polygon || tool == Tool::Line || tool == Tool::Spline) {
            addDraftPoint(world);
            return;
        }
        if (tool == Tool::Frame) {
            addFrame(world);
            return;
        }
        if (const auto node = hitNode(world)) {
            selectedNode = *node;
            if (const forma::NodeDesc* desc = nodeById(*node)) {
                selectedFrame = desc->frame;
            }
            drag = Drag::Node;
            return;
        }
        if (const auto frame = hitFrame(world)) {
            selectedFrame = *frame;
            selectedNode.clear();
            drag = locked(*frame) ? Drag::None : Drag::Frame;
            if (instanceById(*frame)) {
                drag = Drag::Frame;
            }
            return;
        }
        if (const auto primitive = hitPrimitive(world)) {
            selectedPrimitive = *primitive;
            selectedNode.clear();
            status = *primitive;
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
        } else if (drag == Drag::Frame) {
            moveFrame(world);
        } else if (drag == Drag::Canvas && doc.canvas) {
            doc.canvas->width = std::max(32.f, world.x);
            doc.canvas->height = std::max(32.f, world.y);
            status = "canvas " + std::to_string(static_cast<int>(doc.canvas->width)) + " x " +
                     std::to_string(static_cast<int>(doc.canvas->height));
        } else if (drag == Drag::Circle) {
            updateCircle(world);
        }
        lastMouse = screen;
    }

    void drawText(sf::RenderTarget& target, const sf::Font* font, float x, float y, const std::string& text, sf::Color color,
                  unsigned size) const {
        if (font) {
            sf::Text label(*font, sf::String::fromUtf8(text.begin(), text.end()), size);
            label.setPosition({x, y});
            label.setFillColor(color);
            target.draw(label);
            return;
        }
        float cx = x;
        const float scale = size > 15 ? 2.f : 1.6f;
        for (char ch : text) {
            for (int row = 0; row < 7; ++row) {
                const uint8_t bits = glyphRow(ch, row);
                for (int col = 0; col < 5; ++col) {
                    if (bits & (0b10000 >> col)) {
                        sf::RectangleShape pixel({scale, scale});
                        pixel.setPosition({cx + col * scale, y + row * scale});
                        pixel.setFillColor(color);
                        target.draw(pixel);
                    }
                }
            }
            cx += 6.f * scale;
        }
    }

    void drawWorld(sf::RenderTarget& target) {
        const float stepGuess = 40.f;
        float step = stepGuess;
        const float span = 1600.f / std::max(zoom, 0.05f);
        while (span / step > 50.f) {
            step *= 2.f;
        }
        const forma::Vec2 a = cam - forma::Vec2{step, step};
        const forma::Vec2 b = cam + forma::Vec2{span, span};
        sf::VertexArray grid(sf::PrimitiveType::Lines);
        const sf::Color gridColor(0x1c, 0x19, 0x15, 28);
        const int x0 = static_cast<int>(std::floor(a.x / step));
        const int x1 = static_cast<int>(std::ceil(b.x / step));
        const int y0 = static_cast<int>(std::floor(a.y / step));
        const int y1 = static_cast<int>(std::ceil(b.y / step));
        for (int x = x0; x <= x1; ++x) {
            const float px = static_cast<float>(x) * step;
            grid.append(sf::Vertex{{px, a.y}, gridColor});
            grid.append(sf::Vertex{{px, b.y}, gridColor});
        }
        for (int y = y0; y <= y1; ++y) {
            const float py = static_cast<float>(y) * step;
            grid.append(sf::Vertex{{a.x, py}, gridColor});
            grid.append(sf::Vertex{{b.x, py}, gridColor});
        }
        target.draw(grid);

        if (doc.canvas) {
            sf::RectangleShape box({sf::Vector2f(doc.canvas->width, doc.canvas->height)});
            box.setFillColor(sf::Color(0xf4, 0xef, 0xe6, 180));
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
            if (primitive.kind == forma::PrimitiveKind::Polyline || primitive.kind == forma::PrimitiveKind::Catmull) {
                const forma::Polyline2 line = primitive.kind == forma::PrimitiveKind::Polyline
                                                  ? forma::resolve(compiled.pool, primitive.polyline)
                                                  : forma::sample(compiled.pool, primitive.catmull, 0.6f);
                forma::StrokeStyle style = primitive.stroke.value_or(forma::StrokeStyle{2.f, forma::Color::hex(0x1c1915)});
                forma::draw(target, forma::strokePolyline(line, strokeOf(style)));
            } else if (primitive.kind == forma::PrimitiveKind::Polygon || primitive.kind == forma::PrimitiveKind::Circle) {
                const forma::Polygon2 shape = primitive.kind == forma::PrimitiveKind::Polygon
                                                  ? forma::resolve(compiled.pool, primitive.polygon)
                                                  : forma::sample(compiled.pool, primitive.circle, 0.6f);
                if (primitive.fill) {
                    forma::draw(target, forma::fillPolygon(shape, *primitive.fill));
                }
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
            forma::StrokeStyle style;
            style.width = 1.5f;
            style.color = forma::Color::hex(0xc4491d);
            style.cap = forma::LineCap::Round;
            forma::draw(target, forma::strokePolyline(line, style));
        }
        for (forma::Vec2 p : draft) {
            sf::CircleShape dot(4.f / zoom);
            dot.setOrigin({4.f / zoom, 4.f / zoom});
            dot.setPosition({p.x, p.y});
            dot.setFillColor(accent());
            target.draw(dot);
        }

        auto drawAxes = [&](forma::FrameId id, sf::Color color) {
            const forma::Affine xform = compiled.pool.frameWorld(id);
            const forma::Vec2 origin = xform.apply({0.f, 0.f});
            const forma::Vec2 x = xform.apply({22.f, 0.f});
            const forma::Vec2 y = xform.apply({0.f, 22.f});
            sf::VertexArray axes(sf::PrimitiveType::Lines, 4);
            axes[0] = sf::Vertex{{origin.x, origin.y}, color};
            axes[1] = sf::Vertex{{x.x, x.y}, color};
            axes[2] = sf::Vertex{{origin.x, origin.y}, sf::Color(color.r, color.g, color.b, 140)};
            axes[3] = sf::Vertex{{y.x, y.y}, sf::Color(color.r, color.g, color.b, 140)};
            target.draw(axes);
        };
        for (const auto& [id, frame] : compiled.frames) {
            if (id.find('/') != std::string::npos) {
                continue;
            }
            drawAxes(frame, id == selectedFrame ? accent() : sf::Color(0x1c, 0x19, 0x15, 90));
        }

        for (const auto& [id, node] : compiled.nodes) {
            if (locked(id)) {
                continue;
            }
            const forma::Vec2 p = compiled.pool.worldPosition(node);
            const float radius = (id == selectedNode ? 6.f : 4.2f) / zoom;
            sf::CircleShape dot(radius);
            dot.setOrigin({radius, radius});
            dot.setPosition({p.x, p.y});
            dot.setFillColor(id == selectedNode ? accent() : sf::Color(0xf4, 0xef, 0xe6));
            dot.setOutlineColor(ink());
            dot.setOutlineThickness(1.f / zoom);
            target.draw(dot);
        }
    }

    void drawPanel(sf::RenderTarget& target, const sf::Font* font, sf::Vector2u size) {
        sf::RectangleShape panel({kPanel, static_cast<float>(size.y)});
        panel.setFillColor(panelColor());
        target.draw(panel);
        drawText(target, font, 16.f, 14.f, "forma", ink(), 22);
        drawText(target, font, 16.f, 40.f, "editor", accent(), 15);

        rows.clear();
        float y = 72.f;
        drawText(target, font, 16.f, y, "frames", ink(), 13);
        y += 22.f;
        auto depthOf = [&](const std::string& id) {
            int depth = 0;
            std::string cursor = id;
            for (int guard = 0; guard < 32; ++guard) {
                const forma::FrameDesc* frame = nullptr;
                for (const auto& item : doc.frames) {
                    if (item.id == cursor) {
                        frame = &item;
                    }
                }
                if (!frame || frame->parent.empty()) {
                    break;
                }
                cursor = frame->parent;
                ++depth;
            }
            return depth;
        };
        for (const auto& frame : doc.frames) {
            rows.push_back({frame.id, y + 4.f, false});
            const bool selected = frame.id == selectedFrame;
            if (selected) {
                sf::RectangleShape mark({kPanel - 20.f, 16.f});
                mark.setPosition({10.f, y - 2.f});
                mark.setFillColor(sf::Color(0xc4, 0x49, 0x1d, 36));
                target.draw(mark);
            }
            const std::string label = (renaming && selected ? rename + "_" : frame.id);
            drawText(target, font, 16.f + static_cast<float>(depthOf(frame.id)) * 12.f, y, label, selected ? accent() : ink(), 14);
            y += 18.f;
        }
        for (const auto& instance : doc.instances) {
            rows.push_back({instance.id, y + 4.f, true});
            const bool selected = instance.id == selectedFrame;
            drawText(target, font, 16.f, y, "* " + instance.id, selected ? accent() : green(), 14);
            y += 18.f;
        }

        y = std::max(y + 16.f, 250.f);
        const char* kind = doc.kind == forma::DocumentKind::Asset ? "asset" : "world";
        drawText(target, font, 16.f, y, std::string("kind  ") + kind, ink(), 14);
        y += 28.f;
        const char* tools[] = {"1  select", "2  circle", "3  polygon", "4  line", "5  spline", "6  frame"};
        for (int i = 0; i < 6; ++i) {
            drawText(target, font, 16.f, y, tools[i], static_cast<int>(tool) == i ? accent() : ink(), 14);
            y += 18.f;
        }
        y += 12.f;
        drawText(target, font, 16.f, y, "enter ends shape", ink(), 13);
        y += 16.f;
        drawText(target, font, 16.f, y, "n rename   del", ink(), 13);
        y += 16.f;
        drawText(target, font, 16.f, y, "l layer   ctrl-s", ink(), 13);
        y += 16.f;
        drawText(target, font, 16.f, y, "wheel zoom  mmb pan", ink(), 13);

        sf::RectangleShape bar({static_cast<float>(size.x), 28.f});
        bar.setPosition({0.f, static_cast<float>(size.y) - 28.f});
        bar.setFillColor(ink());
        target.draw(bar);
        std::string line = status;
        if (doc.canvas) {
            line += "    canvas " + std::to_string(static_cast<int>(doc.canvas->width)) + "x" +
                    std::to_string(static_cast<int>(doc.canvas->height));
        }
        line += "    " + path;
        drawText(target, font, 12.f, static_cast<float>(size.y) - 22.f, line, paper(), 13);
    }
};

}  // namespace

int main(int argc, char** argv) {
    bool asset = false;
    std::string file;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--asset") {
            asset = true;
        } else if (file.empty()) {
            file = arg;
        }
    }

    sf::RenderWindow window(sf::VideoMode({1280u, 800u}), "forma editor");
    window.setFramerateLimit(60);
    sf::Font font;
    const sf::Font* face = loadFont(font) ? &font : nullptr;

    Editor editor;
    editor.asset = asset;
    if (!file.empty() && std::filesystem::exists(file)) {
        editor.load(file);
        editor.path = file;
    } else {
        editor.doc = asset ? makeAsset() : makeWorld();
        editor.asset = asset;
        editor.selectedFrame = editor.doc.root;
        if (!file.empty()) {
            editor.path = file;
        }
        editor.rebuild();
        editor.status = file.empty() ? editor.status : "new " + file;
    }

    while (window.isOpen()) {
        editor.settleCamera(window);
        const forma::Vec2 mouse{static_cast<float>(sf::Mouse::getPosition(window).x),
                                static_cast<float>(sf::Mouse::getPosition(window).y)};
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
            } else if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
                if (key->code == sf::Keyboard::Key::Escape) {
                    if (editor.renaming) {
                        editor.renaming = false;
                    } else {
                        editor.draft.clear();
                    }
                } else if (key->code == sf::Keyboard::Key::Enter) {
                    if (editor.renaming) {
                        editor.applyRename();
                    } else if (!editor.draft.empty()) {
                        editor.commitDraft();
                    }
                } else if (editor.renaming && key->code == sf::Keyboard::Key::Backspace) {
                    if (!editor.rename.empty()) {
                        editor.rename.pop_back();
                    }
                } else if (!editor.renaming && (key->code == sf::Keyboard::Key::Delete || key->code == sf::Keyboard::Key::Backspace)) {
                    editor.deleteSelection();
                } else if (!editor.renaming && key->control && key->code == sf::Keyboard::Key::S) {
                    editor.save();
                } else if (!editor.renaming && key->code == sf::Keyboard::Key::S) {
                    editor.save();
                } else if (!editor.renaming && key->code == sf::Keyboard::Key::N) {
                    if (!editor.selectedFrame.empty() && !editor.locked(editor.selectedFrame) && editor.frameById(editor.selectedFrame)) {
                        editor.renaming = true;
                        editor.rename = editor.selectedFrame;
                        editor.swallowText = true;
                    }
                } else if (!editor.renaming && key->code == sf::Keyboard::Key::L) {
                    editor.toggleLayer();
                } else if (!editor.renaming && key->code >= sf::Keyboard::Key::Num1 && key->code <= sf::Keyboard::Key::Num6) {
                    editor.tool = static_cast<Editor::Tool>(static_cast<int>(key->code) - static_cast<int>(sf::Keyboard::Key::Num1));
                    editor.draft.clear();
                }
            } else if (const auto* text = event->getIf<sf::Event::TextEntered>()) {
                if (editor.swallowText) {
                    editor.swallowText = false;
                } else if (editor.renaming && text->unicode >= 32 && text->unicode < 127) {
                    const char c = static_cast<char>(text->unicode);
                    if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-') {
                        editor.rename.push_back(c);
                    }
                }
            } else if (const auto* pressed = event->getIf<sf::Event::MouseButtonPressed>()) {
                editor.lastMouse = {static_cast<float>(pressed->position.x), static_cast<float>(pressed->position.y)};
                if (pressed->button == sf::Mouse::Button::Middle || pressed->button == sf::Mouse::Button::Right) {
                    editor.drag = Editor::Drag::Pan;
                } else if (pressed->button == sf::Mouse::Button::Left) {
                    editor.onLeftDown(editor.lastMouse);
                }
            } else if (const auto* released = event->getIf<sf::Event::MouseButtonReleased>()) {
                if (editor.drag == Editor::Drag::Circle) {
                    editor.circleId.clear();
                }
                if (released->button != sf::Mouse::Button::Left || editor.drag != Editor::Drag::None) {
                    editor.drag = Editor::Drag::None;
                }
            } else if (event->is<sf::Event::MouseMoved>()) {
                editor.onMove(mouse);
            } else if (const auto* wheel = event->getIf<sf::Event::MouseWheelScrolled>()) {
                if (wheel->position.x >= kPanel) {
                    const forma::Vec2 cursor{static_cast<float>(wheel->position.x), static_cast<float>(wheel->position.y)};
                    const forma::Vec2 before = editor.screenToWorld(cursor);
                    editor.zoom = std::clamp(editor.zoom * (wheel->delta > 0.f ? 1.1f : 1.f / 1.1f), 0.08f, 8.f);
                    editor.cam = before - (cursor - forma::Vec2{kPanel, 0.f}) / editor.zoom;
                }
            } else if (const auto* resized = event->getIf<sf::Event::Resized>()) {
                sf::View view;
                view.setSize({static_cast<float>(resized->size.x), static_cast<float>(resized->size.y)});
                view.setCenter(view.getSize() * 0.5f);
                window.setView(view);
            }
        }

        window.clear(paper());
        window.setView(editor.worldView(window));
        editor.drawWorld(window);
        sf::View ui;
        ui.setSize({static_cast<float>(window.getSize().x), static_cast<float>(window.getSize().y)});
        ui.setCenter(ui.getSize() * 0.5f);
        window.setView(ui);
        editor.drawPanel(window, face, window.getSize());
        window.display();
    }
    return 0;
}
