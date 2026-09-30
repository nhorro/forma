#include "forma/document.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace forma {
namespace {

constexpr float kPi = 3.14159265358979323846f;

std::string fail(int line, const std::string& message) {
    return "forma json line " + std::to_string(line) + ": " + message;
}

struct Val {
    enum class Type { Null, Bool, Num, Str, Arr, Obj };
    Type type = Type::Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Val> a;
    std::vector<std::pair<std::string, Val>> o;

    const Val* find(std::string_view key) const {
        const Val* found = nullptr;
        for (const auto& kv : o) {
            if (kv.first == key) {
                found = &kv.second;
            }
        }
        return found;
    }

    const Val& need(std::string_view key, int line) const {
        const Val* value = find(key);
        if (!value) {
            throw std::runtime_error(fail(line, "missing " + std::string(key)));
        }
        return *value;
    }
};

struct Parser {
    std::string_view in;
    std::size_t i = 0;
    int line = 1;

    [[noreturn]] void error(const std::string& message) const { throw std::runtime_error(fail(line, message)); }

    void skip() {
        while (i < in.size()) {
            const char c = in[i];
            if (c == '\n') {
                ++line;
                ++i;
            } else if (c == ' ' || c == '\t' || c == '\r') {
                ++i;
            } else {
                break;
            }
        }
    }

    char peek() {
        skip();
        return i < in.size() ? in[i] : '\0';
    }

    char getc() {
        skip();
        if (i >= in.size()) {
            error("unexpected end");
        }
        return in[i++];
    }

    Val parse() {
        const char c = peek();
        if (c == '{') {
            return parseObject();
        }
        if (c == '[') {
            return parseArray();
        }
        if (c == '"') {
            return parseString();
        }
        if (c == 't' || c == 'f') {
            return parseBool();
        }
        if (c == 'n') {
            return parseNull();
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return parseNumber();
        }
        error("unexpected token");
    }

    Val parseObject() {
        if (getc() != '{') {
            error("expected '{'");
        }
        Val value;
        value.type = Val::Type::Obj;
        if (peek() == '}') {
            getc();
            return value;
        }
        while (true) {
            if (peek() != '"') {
                error("expected key");
            }
            Val key = parseString();
            if (getc() != ':') {
                error("expected ':'");
            }
            value.o.emplace_back(std::move(key.s), parse());
            const char next = peek();
            if (next == ',') {
                getc();
                if (peek() == '}') {
                    getc();
                    break;
                }
                continue;
            }
            if (next == '}') {
                getc();
                break;
            }
            error("expected ',' or '}'");
        }
        return value;
    }

    Val parseArray() {
        if (getc() != '[') {
            error("expected '['");
        }
        Val value;
        value.type = Val::Type::Arr;
        if (peek() == ']') {
            getc();
            return value;
        }
        while (true) {
            value.a.push_back(parse());
            const char next = peek();
            if (next == ',') {
                getc();
                if (peek() == ']') {
                    getc();
                    break;
                }
                continue;
            }
            if (next == ']') {
                getc();
                break;
            }
            error("expected ',' or ']'");
        }
        return value;
    }

    Val parseString() {
        if (getc() != '"') {
            error("expected string");
        }
        Val value;
        value.type = Val::Type::Str;
        while (i < in.size()) {
            const char c = in[i++];
            if (c == '\n') {
                ++line;
            }
            if (c == '"') {
                return value;
            }
            if (c != '\\') {
                value.s.push_back(c);
                continue;
            }
            if (i >= in.size()) {
                error("bad escape");
            }
            const char e = in[i++];
            switch (e) {
                case '"':
                case '\\':
                case '/':
                    value.s.push_back(e);
                    break;
                case 'b':
                    value.s.push_back('\b');
                    break;
                case 'f':
                    value.s.push_back('\f');
                    break;
                case 'n':
                    value.s.push_back('\n');
                    break;
                case 'r':
                    value.s.push_back('\r');
                    break;
                case 't':
                    value.s.push_back('\t');
                    break;
                case 'u': {
                    if (i + 4 > in.size()) {
                        error("bad unicode escape");
                    }
                    int code = 0;
                    for (int k = 0; k < 4; ++k) {
                        const char h = in[i++];
                        code <<= 4;
                        if (h >= '0' && h <= '9') {
                            code += h - '0';
                        } else if (h >= 'a' && h <= 'f') {
                            code += h - 'a' + 10;
                        } else if (h >= 'A' && h <= 'F') {
                            code += h - 'A' + 10;
                        } else {
                            error("bad unicode escape");
                        }
                    }
                    if (code < 0x80) {
                        value.s.push_back(static_cast<char>(code));
                    } else if (code < 0x800) {
                        value.s.push_back(static_cast<char>(0xC0 | (code >> 6)));
                        value.s.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    } else {
                        value.s.push_back(static_cast<char>(0xE0 | (code >> 12)));
                        value.s.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                        value.s.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    }
                    break;
                }
                default:
                    error("bad escape");
            }
        }
        error("unterminated string");
    }

    Val parseBool() {
        Val value;
        value.type = Val::Type::Bool;
        if (in.compare(i, 4, "true") == 0) {
            value.b = true;
            i += 4;
            return value;
        }
        if (in.compare(i, 5, "false") == 0) {
            value.b = false;
            i += 5;
            return value;
        }
        error("expected boolean");
    }

    Val parseNull() {
        if (in.compare(i, 4, "null") != 0) {
            error("expected null");
        }
        i += 4;
        Val value;
        value.type = Val::Type::Null;
        return value;
    }

    Val parseNumber() {
        const std::size_t start = i;
        if (peek() == '-') {
            ++i;
        }
        if (i >= in.size() || in[i] < '0' || in[i] > '9') {
            error("expected number");
        }
        while (i < in.size() && in[i] >= '0' && in[i] <= '9') {
            ++i;
        }
        if (i < in.size() && in[i] == '.') {
            ++i;
            while (i < in.size() && in[i] >= '0' && in[i] <= '9') {
                ++i;
            }
        }
        if (i < in.size() && (in[i] == 'e' || in[i] == 'E')) {
            ++i;
            if (i < in.size() && (in[i] == '+' || in[i] == '-')) {
                ++i;
            }
            while (i < in.size() && in[i] >= '0' && in[i] <= '9') {
                ++i;
            }
        }
        const std::string token(in.substr(start, i - start));
        char* end = nullptr;
        Val value;
        value.type = Val::Type::Num;
        value.n = std::strtod(token.c_str(), &end);
        if (end == token.c_str()) {
            error("bad number");
        }
        return value;
    }
};

float num(const Val& value, int line) {
    if (value.type != Val::Type::Num) {
        throw std::runtime_error(fail(line, "expected number"));
    }
    return static_cast<float>(value.n);
}

const std::string& str(const Val& value, int line) {
    if (value.type != Val::Type::Str) {
        throw std::runtime_error(fail(line, "expected string"));
    }
    return value.s;
}

Vec2 vec2Of(const Val& value, int line) {
    if (value.type != Val::Type::Arr || value.a.size() != 2) {
        throw std::runtime_error(fail(line, "expected [x, y]"));
    }
    return {num(value.a[0], line), num(value.a[1], line)};
}

Vec2 scaleOf(const Val& value, int line) {
    if (value.type == Val::Type::Num) {
        const float s = num(value, line);
        return {s, s};
    }
    return vec2Of(value, line);
}

int hexNibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

Color colorOf(const Val& value, int line) {
    const std::string& text = str(value, line);
    if (text.size() != 7 && text.size() != 9) {
        throw std::runtime_error(fail(line, "color wants #RRGGBB"));
    }
    if (text[0] != '#') {
        throw std::runtime_error(fail(line, "color wants #RRGGBB"));
    }
    int channel[4] = {0, 0, 0, 255};
    const int pairs = text.size() == 9 ? 4 : 3;
    for (int i = 0; i < pairs; ++i) {
        const int hi = hexNibble(text[1 + i * 2]);
        const int lo = hexNibble(text[2 + i * 2]);
        if (hi < 0 || lo < 0) {
            throw std::runtime_error(fail(line, "bad color"));
        }
        channel[i] = hi * 16 + lo;
    }
    return Color::rgba(static_cast<uint8_t>(channel[0]), static_cast<uint8_t>(channel[1]),
                       static_cast<uint8_t>(channel[2]), static_cast<uint8_t>(channel[3]));
}

std::string hexOf(Color color) {
    char buf[16];
    if (color.a == 255) {
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", color.r, color.g, color.b);
    } else {
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", color.r, color.g, color.b, color.a);
    }
    return buf;
}

FramePose poseOf(const Val& object, int line) {
    FramePose pose;
    if (const Val* t = object.find("t")) {
        pose.translation = vec2Of(*t, line);
    }
    if (const Val* r = object.find("r")) {
        pose.rotation = num(*r, line) * (kPi / 180.f);
    }
    if (const Val* s = object.find("s")) {
        pose.scale = scaleOf(*s, line);
    }
    return pose;
}

void inheritOf(const Val& object, bool& translation, bool& rotation, bool& scale) {
    const Val* inherit = object.find("inherit");
    if (!inherit || inherit->type == Val::Type::Null) {
        return;
    }
    if (inherit->type != Val::Type::Obj) {
        throw std::runtime_error("forma json: inherit wants an object");
    }
    if (const Val* t = inherit->find("t")) {
        if (t->type != Val::Type::Bool) {
            throw std::runtime_error("forma json: inherit.t wants a bool");
        }
        translation = t->b;
    }
    if (const Val* r = inherit->find("r")) {
        if (r->type != Val::Type::Bool) {
            throw std::runtime_error("forma json: inherit.r wants a bool");
        }
        rotation = r->b;
    }
    if (const Val* s = inherit->find("s")) {
        if (s->type != Val::Type::Bool) {
            throw std::runtime_error("forma json: inherit.s wants a bool");
        }
        scale = s->b;
    }
}

PrimitiveKind kindOf(const std::string& name) {
    if (name == "polyline") {
        return PrimitiveKind::Polyline;
    }
    if (name == "catmull") {
        return PrimitiveKind::Catmull;
    }
    if (name == "polygon") {
        return PrimitiveKind::Polygon;
    }
    if (name == "circle") {
        return PrimitiveKind::Circle;
    }
    throw std::runtime_error("forma json: unknown primitive " + name);
}

const char* kindName(PrimitiveKind kind) {
    switch (kind) {
        case PrimitiveKind::Polyline:
            return "polyline";
        case PrimitiveKind::Catmull:
            return "catmull";
        case PrimitiveKind::Polygon:
            return "polygon";
        case PrimitiveKind::Circle:
            return "circle";
    }
    return "polyline";
}

CurveParameterization paramOf(const std::string& name) {
    if (name == "uniform") {
        return CurveParameterization::Uniform;
    }
    if (name == "chordal") {
        return CurveParameterization::Chordal;
    }
    if (name == "centripetal") {
        return CurveParameterization::Centripetal;
    }
    throw std::runtime_error("forma json: unknown parameterization " + name);
}

const char* paramName(CurveParameterization param) {
    switch (param) {
        case CurveParameterization::Uniform:
            return "uniform";
        case CurveParameterization::Chordal:
            return "chordal";
        case CurveParameterization::Centripetal:
            return "centripetal";
    }
    return "centripetal";
}

LineCap capOf(const std::string& name) {
    if (name == "butt") {
        return LineCap::Butt;
    }
    if (name == "round") {
        return LineCap::Round;
    }
    throw std::runtime_error("forma json: unknown cap " + name);
}

LineJoin joinOf(const std::string& name) {
    if (name == "bevel") {
        return LineJoin::Bevel;
    }
    if (name == "round") {
        return LineJoin::Round;
    }
    throw std::runtime_error("forma json: unknown join " + name);
}

std::optional<StrokeStyle> strokeOf(const Val* value) {
    if (!value || value->type == Val::Type::Null) {
        return std::nullopt;
    }
    if (value->type != Val::Type::Obj) {
        throw std::runtime_error("forma json: stroke wants an object");
    }
    StrokeStyle stroke;
    if (const Val* width = value->find("width")) {
        stroke.width = num(*width, 0);
    }
    if (const Val* color = value->find("color")) {
        stroke.color = colorOf(*color, 0);
    }
    if (const Val* cap = value->find("cap")) {
        stroke.cap = capOf(str(*cap, 0));
    }
    if (const Val* join = value->find("join")) {
        stroke.join = joinOf(str(*join, 0));
    }
    return stroke;
}

struct Writer {
    std::string out;
    int indent = 0;

    void raw(std::string_view text) { out.append(text); }
    void nl() {
        out.push_back('\n');
        out.append(static_cast<std::size_t>(indent) * 2, ' ');
    }
};

std::string escape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (unsigned char c : text) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

std::string numStr(float value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(value));
    return buf;
}

void writePoseFields(Writer& w, const FramePose& pose, bool inheritT, bool inheritR, bool inheritS, bool withInherit) {
    w.raw("\"t\": [");
    w.raw(numStr(pose.translation.x));
    w.raw(", ");
    w.raw(numStr(pose.translation.y));
    w.raw("], \"r\": ");
    w.raw(numStr(pose.rotation * (180.f / kPi)));
    w.raw(", \"s\": [");
    w.raw(numStr(pose.scale.x));
    w.raw(", ");
    w.raw(numStr(pose.scale.y));
    w.raw("]");
    if (withInherit && !(inheritT && inheritR && inheritS)) {
        w.raw(", \"inherit\": {\"t\": ");
        w.raw(inheritT ? "true" : "false");
        w.raw(", \"r\": ");
        w.raw(inheritR ? "true" : "false");
        w.raw(", \"s\": ");
        w.raw(inheritS ? "true" : "false");
        w.raw("}");
    }
}

void compileInto(const Document& document, NodePool& pool, FrameId attachUnder, const std::string& prefix,
                 CompiledDocument& out, const AssetLoad& load, int depth) {
    if (depth > 8) {
        throw std::runtime_error("instance nesting is too deep");
    }
    if (document.version != 1) {
        throw std::runtime_error("unsupported forma version");
    }
    const std::string rootName = document.root.empty() ? "root" : document.root;
    const FrameDesc* rootDesc = nullptr;
    for (const FrameDesc& frame : document.frames) {
        if (frame.id == rootName) {
            rootDesc = &frame;
        }
    }
    if (!document.frames.empty() && !rootDesc) {
        throw std::runtime_error("root frame '" + rootName + "' is missing");
    }
    if (rootDesc && !rootDesc->parent.empty() && !attachUnder.valid()) {
        throw std::runtime_error("root frame cannot have a parent");
    }

    std::unordered_map<std::string, FrameId> local;
    auto bind = [&](const std::string& name, FrameId id) {
        if (!local.emplace(name, id).second) {
            throw std::runtime_error("duplicate frame '" + name + "'");
        }
        if (!out.frames.emplace(prefix + name, id).second) {
            throw std::runtime_error("duplicate frame '" + prefix + name + "'");
        }
    };

    const FramePose rootPose = rootDesc ? rootDesc->pose : FramePose{};
    const bool inheritT = rootDesc ? rootDesc->inheritTranslation : true;
    const bool inheritR = rootDesc ? rootDesc->inheritRotation : true;
    const bool inheritS = rootDesc ? rootDesc->inheritScale : true;
    FrameId rootId;
    if (!attachUnder.valid()) {
        rootId = pool.root();
    } else {
        rootId = pool.createFrame(attachUnder);
    }
    pool.placePose(rootId, rootPose);
    pool.setInherit(rootId, inheritT, inheritR, inheritS);
    bind(rootName, rootId);

    std::vector<const FrameDesc*> pending;
    for (const FrameDesc& frame : document.frames) {
        if (frame.id != rootName) {
            pending.push_back(&frame);
        }
    }
    while (!pending.empty()) {
        bool progress = false;
        for (auto it = pending.begin(); it != pending.end();) {
            const FrameDesc& frame = **it;
            if (frame.id.empty()) {
                throw std::runtime_error("frame is missing an id");
            }
            const std::string parentName = frame.parent.empty() ? rootName : frame.parent;
            const auto parent = local.find(parentName);
            if (parent == local.end()) {
                ++it;
                continue;
            }
            const FrameId id = pool.createFrame(parent->second);
            pool.placePose(id, frame.pose);
            pool.setInherit(id, frame.inheritTranslation, frame.inheritRotation, frame.inheritScale);
            bind(frame.id, id);
            it = pending.erase(it);
            progress = true;
        }
        if (!progress) {
            throw std::runtime_error("frame parent is missing or cyclic");
        }
    }

    for (const NodeDesc& node : document.nodes) {
        if (node.id.empty()) {
            throw std::runtime_error("node is missing an id");
        }
        const std::string frameName = node.frame.empty() ? rootName : node.frame;
        const auto frame = local.find(frameName);
        if (frame == local.end()) {
            throw std::runtime_error("node '" + node.id + "' has no frame '" + frameName + "'");
        }
        const NodeId id = pool.create(frame->second, node.position);
        if (!out.nodes.emplace(prefix + node.id, id).second) {
            throw std::runtime_error("duplicate node '" + prefix + node.id + "'");
        }
    }

    auto resolveNode = [&](const std::string& name) {
        const auto it = out.nodes.find(prefix + name);
        if (it == out.nodes.end()) {
            throw std::runtime_error("unknown node '" + name + "'");
        }
        return it->second;
    };

    for (const PrimitiveDesc& primitive : document.primitives) {
        if (primitive.id.empty()) {
            throw std::runtime_error("primitive is missing an id");
        }
        CompiledPrimitive compiled;
        compiled.id = prefix + primitive.id;
        compiled.kind = primitive.kind;
        compiled.fill = primitive.fill;
        compiled.stroke = primitive.stroke;
        compiled.layer = primitive.layer;
        auto same = [&](const std::vector<NodeId>& ids) {
            if (ids.empty()) {
                return;
            }
            const FrameId frame = pool.frame(ids.front());
            for (NodeId id : ids) {
                if (pool.frame(id) != frame) {
                    throw std::runtime_error("primitive '" + primitive.id + "' crosses frames");
                }
            }
        };
        switch (primitive.kind) {
            case PrimitiveKind::Polyline:
                compiled.polyline.closed = primitive.closed;
                for (const std::string& name : primitive.nodes) {
                    compiled.polyline.nodes.push_back(resolveNode(name));
                }
                same(compiled.polyline.nodes);
                break;
            case PrimitiveKind::Catmull:
                compiled.catmull.closed = primitive.closed;
                compiled.catmull.curve = primitive.curve;
                compiled.catmull.parameterization = primitive.parameterization;
                for (const std::string& name : primitive.nodes) {
                    compiled.catmull.nodes.push_back(resolveNode(name));
                }
                same(compiled.catmull.nodes);
                break;
            case PrimitiveKind::Polygon:
                for (const std::string& name : primitive.nodes) {
                    compiled.polygon.nodes.push_back(resolveNode(name));
                }
                same(compiled.polygon.nodes);
                break;
            case PrimitiveKind::Circle:
                if (primitive.center.empty()) {
                    throw std::runtime_error("circle '" + primitive.id + "' needs a center");
                }
                compiled.circle.center = resolveNode(primitive.center);
                compiled.circle.radius = primitive.radius;
                break;
        }
        out.primitives.push_back(std::move(compiled));
    }

    for (const InstanceDesc& instance : document.instances) {
        if (!load) {
            throw std::runtime_error("instance '" + instance.id + "' has no asset loader");
        }
        if (instance.id.empty() || instance.asset.empty()) {
            throw std::runtime_error("instance needs an id and an asset");
        }
        const std::string parentName = instance.parent.empty() ? rootName : instance.parent;
        const auto parent = local.find(parentName);
        if (parent == local.end()) {
            throw std::runtime_error("instance '" + instance.id + "' has no parent '" + parentName + "'");
        }
        const FrameId slot = pool.createFrame(parent->second);
        pool.placePose(slot, instance.pose);
        if (!out.frames.emplace(prefix + instance.id, slot).second) {
            throw std::runtime_error("duplicate instance '" + prefix + instance.id + "'");
        }
        const Document asset = load(instance.asset);
        compileInto(asset, pool, slot, prefix + instance.id + "/", out, load, depth + 1);
    }
}

}  // namespace

CompiledDocument compile(const Document& document, const AssetLoad& load) {
    CompiledDocument compiled;
    compileInto(document, compiled.pool, FrameId{}, "", compiled, load, 0);
    return compiled;
}

void writePose(Document& document, const CompiledDocument& compiled) {
    for (FrameDesc& frame : document.frames) {
        const auto it = compiled.frames.find(frame.id);
        if (it == compiled.frames.end()) {
            continue;
        }
        frame.pose = compiled.pool.pose(it->second);
    }
    for (NodeDesc& node : document.nodes) {
        const auto it = compiled.nodes.find(node.id);
        if (it == compiled.nodes.end()) {
            continue;
        }
        node.position = compiled.pool.get(it->second);
    }
    for (InstanceDesc& instance : document.instances) {
        const auto it = compiled.frames.find(instance.id);
        if (it == compiled.frames.end()) {
            continue;
        }
        instance.pose = compiled.pool.pose(it->second);
    }
}

Document documentFromJson(std::string_view json) {
    Parser parser;
    parser.in = json;
    const Val root = parser.parse();
    parser.skip();
    if (parser.i < parser.in.size()) {
        parser.error("trailing data");
    }
    if (root.type != Val::Type::Obj) {
        throw std::runtime_error("forma json: expected an object");
    }
    Document document;
    if (const Val* version = root.find("forma")) {
        document.version = static_cast<int>(num(*version, parser.line));
    }
    if (const Val* kind = root.find("kind")) {
        const std::string& name = str(*kind, parser.line);
        if (name == "asset") {
            document.kind = DocumentKind::Asset;
        } else if (name == "world") {
            document.kind = DocumentKind::World;
        } else {
            throw std::runtime_error("forma json: kind wants asset or world");
        }
    }
    if (const Val* name = root.find("name")) {
        document.name = str(*name, parser.line);
    }
    if (const Val* rootId = root.find("root")) {
        document.root = str(*rootId, parser.line);
    }
    if (const Val* canvas = root.find("canvas")) {
        if (canvas->type != Val::Type::Null) {
            if (canvas->type != Val::Type::Obj) {
                throw std::runtime_error("forma json: canvas wants an object");
            }
            Canvas box;
            box.width = num(canvas->need("width", parser.line), parser.line);
            box.height = num(canvas->need("height", parser.line), parser.line);
            document.canvas = box;
        }
    }
    if (const Val* frames = root.find("frames")) {
        if (frames->type != Val::Type::Arr) {
            throw std::runtime_error("forma json: frames wants an array");
        }
        for (const Val& item : frames->a) {
            if (item.type != Val::Type::Obj) {
                throw std::runtime_error("forma json: frame wants an object");
            }
            FrameDesc frame;
            frame.id = str(item.need("id", parser.line), parser.line);
            if (const Val* parent = item.find("parent")) {
                if (parent->type != Val::Type::Null) {
                    frame.parent = str(*parent, parser.line);
                }
            }
            frame.pose = poseOf(item, parser.line);
            inheritOf(item, frame.inheritTranslation, frame.inheritRotation, frame.inheritScale);
            document.frames.push_back(std::move(frame));
        }
    }
    if (const Val* nodes = root.find("nodes")) {
        if (nodes->type != Val::Type::Arr) {
            throw std::runtime_error("forma json: nodes wants an array");
        }
        for (const Val& item : nodes->a) {
            NodeDesc node;
            node.id = str(item.need("id", parser.line), parser.line);
            if (const Val* frame = item.find("frame")) {
                node.frame = str(*frame, parser.line);
            }
            if (const Val* p = item.find("p")) {
                node.position = vec2Of(*p, parser.line);
            }
            document.nodes.push_back(std::move(node));
        }
    }
    if (const Val* primitives = root.find("primitives")) {
        if (primitives->type != Val::Type::Arr) {
            throw std::runtime_error("forma json: primitives wants an array");
        }
        for (const Val& item : primitives->a) {
            PrimitiveDesc primitive;
            primitive.id = str(item.need("id", parser.line), parser.line);
            primitive.kind = kindOf(str(item.need("kind", parser.line), parser.line));
            if (const Val* nodes = item.find("nodes")) {
                if (nodes->type != Val::Type::Arr) {
                    throw std::runtime_error("forma json: nodes wants an array");
                }
                for (const Val& name : nodes->a) {
                    primitive.nodes.push_back(str(name, parser.line));
                }
            }
            if (const Val* center = item.find("center")) {
                primitive.center = str(*center, parser.line);
            }
            if (const Val* radius = item.find("radius")) {
                primitive.radius = num(*radius, parser.line);
            }
            if (const Val* closed = item.find("closed")) {
                if (closed->type != Val::Type::Bool) {
                    throw std::runtime_error("forma json: closed wants a bool");
                }
                primitive.closed = closed->b;
            }
            if (const Val* curve = item.find("curve")) {
                primitive.curve = num(*curve, parser.line);
            }
            if (const Val* param = item.find("parameterization")) {
                primitive.parameterization = paramOf(str(*param, parser.line));
            }
            if (const Val* fill = item.find("fill")) {
                if (fill->type != Val::Type::Null) {
                    primitive.fill = colorOf(*fill, parser.line);
                }
            }
            primitive.stroke = strokeOf(item.find("stroke"));
            if (const Val* layer = item.find("layer")) {
                primitive.layer = str(*layer, parser.line);
            }
            document.primitives.push_back(std::move(primitive));
        }
    }
    if (const Val* instances = root.find("instances")) {
        if (instances->type != Val::Type::Arr) {
            throw std::runtime_error("forma json: instances wants an array");
        }
        for (const Val& item : instances->a) {
            InstanceDesc instance;
            instance.id = str(item.need("id", parser.line), parser.line);
            instance.asset = str(item.need("asset", parser.line), parser.line);
            if (const Val* parent = item.find("parent")) {
                instance.parent = str(*parent, parser.line);
            }
            instance.pose = poseOf(item, parser.line);
            document.instances.push_back(std::move(instance));
        }
    }
    if (const Val* layers = root.find("layers")) {
        if (layers->type != Val::Type::Arr) {
            throw std::runtime_error("forma json: layers wants an array");
        }
        for (const Val& item : layers->a) {
            LayerDesc layer;
            layer.id = str(item.need("id", parser.line), parser.line);
            if (const Val* order = item.find("order")) {
                layer.order = static_cast<int>(num(*order, parser.line));
            }
            if (const Val* role = item.find("role")) {
                const std::string& name = str(*role, parser.line);
                if (name == "collision") {
                    layer.role = LayerRole::Collision;
                } else if (name == "guide") {
                    layer.role = LayerRole::Guide;
                } else if (name == "visual") {
                    layer.role = LayerRole::Visual;
                } else {
                    throw std::runtime_error("forma json: unknown layer role");
                }
            }
            document.layers.push_back(std::move(layer));
        }
    }
    return document;
}

std::string toJson(const Document& document) {
    Writer w;
    w.raw("{");
    w.indent++;
    w.nl();
    w.raw("\"forma\": 1,");
    w.nl();
    w.raw("\"kind\": \"");
    w.raw(document.kind == DocumentKind::Asset ? "asset" : "world");
    w.raw("\",");
    w.nl();
    if (!document.name.empty()) {
        w.raw("\"name\": \"");
        w.raw(escape(document.name));
        w.raw("\",");
        w.nl();
    }
    w.raw("\"root\": \"");
    w.raw(escape(document.root));
    w.raw("\",");
    w.nl();
    if (document.canvas) {
        w.raw("\"canvas\": {\"width\": ");
        w.raw(numStr(document.canvas->width));
        w.raw(", \"height\": ");
        w.raw(numStr(document.canvas->height));
        w.raw("},");
        w.nl();
    }
    w.raw("\"frames\": [");
    w.indent++;
    for (std::size_t i = 0; i < document.frames.size(); ++i) {
        const FrameDesc& frame = document.frames[i];
        w.nl();
        w.raw("{\"id\": \"");
        w.raw(escape(frame.id));
        w.raw("\"");
        if (!frame.parent.empty()) {
            w.raw(", \"parent\": \"");
            w.raw(escape(frame.parent));
            w.raw("\"");
        }
        w.raw(", ");
        writePoseFields(w, frame.pose, frame.inheritTranslation, frame.inheritRotation, frame.inheritScale, true);
        w.raw("}");
        if (i + 1 < document.frames.size()) {
            w.raw(",");
        }
    }
    w.indent--;
    if (!document.frames.empty()) {
        w.nl();
    }
    w.raw("],");
    w.nl();
    w.raw("\"nodes\": [");
    w.indent++;
    for (std::size_t i = 0; i < document.nodes.size(); ++i) {
        const NodeDesc& node = document.nodes[i];
        w.nl();
        w.raw("{\"id\": \"");
        w.raw(escape(node.id));
        w.raw("\", \"frame\": \"");
        w.raw(escape(node.frame));
        w.raw("\", \"p\": [");
        w.raw(numStr(node.position.x));
        w.raw(", ");
        w.raw(numStr(node.position.y));
        w.raw("]}");
        if (i + 1 < document.nodes.size()) {
            w.raw(",");
        }
    }
    w.indent--;
    if (!document.nodes.empty()) {
        w.nl();
    }
    w.raw("],");
    w.nl();
    w.raw("\"primitives\": [");
    w.indent++;
    for (std::size_t i = 0; i < document.primitives.size(); ++i) {
        const PrimitiveDesc& primitive = document.primitives[i];
        w.nl();
        w.raw("{\"id\": \"");
        w.raw(escape(primitive.id));
        w.raw("\", \"kind\": \"");
        w.raw(kindName(primitive.kind));
        w.raw("\"");
        if (primitive.kind == PrimitiveKind::Circle) {
            w.raw(", \"center\": \"");
            w.raw(escape(primitive.center));
            w.raw("\", \"radius\": ");
            w.raw(numStr(primitive.radius));
        } else {
            w.raw(", \"nodes\": [");
            for (std::size_t n = 0; n < primitive.nodes.size(); ++n) {
                if (n) {
                    w.raw(", ");
                }
                w.raw("\"");
                w.raw(escape(primitive.nodes[n]));
                w.raw("\"");
            }
            w.raw("]");
            if (primitive.kind != PrimitiveKind::Polygon && primitive.closed) {
                w.raw(", \"closed\": true");
            }
            if (primitive.kind == PrimitiveKind::Catmull) {
                w.raw(", \"curve\": ");
                w.raw(numStr(primitive.curve));
                w.raw(", \"parameterization\": \"");
                w.raw(paramName(primitive.parameterization));
                w.raw("\"");
            }
        }
        if (primitive.fill) {
            w.raw(", \"fill\": \"");
            w.raw(hexOf(*primitive.fill));
            w.raw("\"");
        }
        if (primitive.stroke) {
            w.raw(", \"stroke\": {\"width\": ");
            w.raw(numStr(primitive.stroke->width));
            w.raw(", \"color\": \"");
            w.raw(hexOf(primitive.stroke->color));
            w.raw("\", \"cap\": \"");
            w.raw(primitive.stroke->cap == LineCap::Butt ? "butt" : "round");
            w.raw("\", \"join\": \"");
            w.raw(primitive.stroke->join == LineJoin::Bevel ? "bevel" : "round");
            w.raw("\"}");
        }
        if (!primitive.layer.empty()) {
            w.raw(", \"layer\": \"");
            w.raw(escape(primitive.layer));
            w.raw("\"");
        }
        w.raw("}");
        if (i + 1 < document.primitives.size()) {
            w.raw(",");
        }
    }
    w.indent--;
    if (!document.primitives.empty()) {
        w.nl();
    }
    w.raw("]");
    if (!document.instances.empty()) {
        w.raw(",");
        w.nl();
        w.raw("\"instances\": [");
        w.indent++;
        for (std::size_t i = 0; i < document.instances.size(); ++i) {
            const InstanceDesc& instance = document.instances[i];
            w.nl();
            w.raw("{\"id\": \"");
            w.raw(escape(instance.id));
            w.raw("\", \"asset\": \"");
            w.raw(escape(instance.asset));
            w.raw("\"");
            if (!instance.parent.empty()) {
                w.raw(", \"parent\": \"");
                w.raw(escape(instance.parent));
                w.raw("\"");
            }
            w.raw(", ");
            writePoseFields(w, instance.pose, true, true, true, false);
            w.raw("}");
            if (i + 1 < document.instances.size()) {
                w.raw(",");
            }
        }
        w.indent--;
        if (!document.instances.empty()) {
            w.nl();
        }
        w.raw("]");
    }
    if (!document.layers.empty()) {
        w.raw(",");
        w.nl();
        w.raw("\"layers\": [");
        w.indent++;
        for (std::size_t i = 0; i < document.layers.size(); ++i) {
            const LayerDesc& layer = document.layers[i];
            w.nl();
            w.raw("{\"id\": \"");
            w.raw(escape(layer.id));
            w.raw("\", \"order\": ");
            w.raw(std::to_string(layer.order));
            w.raw(", \"role\": \"");
            const char* role = "visual";
            if (layer.role == LayerRole::Collision) {
                role = "collision";
            } else if (layer.role == LayerRole::Guide) {
                role = "guide";
            }
            w.raw(role);
            w.raw("\"}");
            if (i + 1 < document.layers.size()) {
                w.raw(",");
            }
        }
        w.indent--;
        w.nl();
        w.raw("]");
    }
    w.indent--;
    w.nl();
    w.raw("}\n");
    return w.out;
}

}  // namespace forma
