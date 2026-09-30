#include "forma/document.hpp"
#include "forma/forma.hpp"
#include "forma/sfml_draw.hpp"

#include <SFML/Graphics.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string readFile(const char* path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error(std::string("could not read ") + path);
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

forma::Vec2 stepOffset(float phase, float stride, float lift) {
    const float p = phase - std::floor(phase);
    if (p < 0.5f) {
        return {(0.5f - p / 0.5f) * stride, 0.f};
    }
    const float u = (p - 0.5f) / 0.5f;
    return {(-0.5f + u) * stride, -std::sin(u * 3.14159265f) * lift};
}

forma::Vec2 tipOf(const forma::CompiledDocument& doc, forma::FrameId frame) {
    forma::Vec2 best{};
    float bestDistance = 0.f;
    for (const auto& [id, node] : doc.nodes) {
        (void)id;
        if (doc.pool.frame(node) != frame) {
            continue;
        }
        const forma::Vec2 local = doc.pool.get(node);
        const float span = forma::length(local);
        if (span > bestDistance) {
            bestDistance = span;
            best = local;
        }
    }
    return best;
}

struct Limb {
    forma::FrameId upper{};
    forma::FrameId lower{};
    forma::Vec2 tip{};
    forma::Vec2 rest{};
    float phase = 0.f;
};

struct Walker {
    forma::CompiledDocument doc;
    forma::FrameId root{};
    std::vector<Limb> legs;
    forma::Vec2 place{};
    const forma::Clip* swing = nullptr;

    void load(const char* path, forma::Vec2 at, std::vector<std::pair<const char*, float>> legsIn) {
        doc = forma::compile(forma::documentFromJson(readFile(path)));
        root = doc.frames.at("pelvis");
        place = at;
        const forma::Vec2 origin = doc.pool.frameWorld(root).apply({0.f, 0.f});
        for (const auto& [name, phase] : legsIn) {
            Limb limb;
            const std::string thigh = std::string("thigh.") + name;
            const std::string shin = std::string("shin.") + name;
            const std::string upper = doc.frames.count(thigh) ? thigh : std::string("legU.") + name;
            const std::string lower = doc.frames.count(shin) ? shin : std::string("legL.") + name;
            limb.upper = doc.frames.at(upper);
            limb.lower = doc.frames.at(lower);
            limb.tip = tipOf(doc, limb.lower);
            limb.rest = doc.pool.frameWorld(limb.lower).apply(limb.tip) - origin;
            limb.phase = phase;
            legs.push_back(limb);
        }
    }

    void tick(float time, float stride, float lift) {
        forma::FramePose pose = doc.pool.pose(root);
        pose.translation = place + forma::Vec2{std::sin(time * 2.f) * 18.f, std::sin(time * 4.f) * 4.f};
        doc.pool.setPose(root, pose);
        const forma::Vec2 origin = doc.pool.frameWorld(root).apply({0.f, 0.f});
        for (const Limb& limb : legs) {
            const forma::FrameId chain[] = {limb.upper, limb.lower};
            const forma::Vec2 target = origin + limb.rest + stepOffset(time * 1.4f + limb.phase, stride, lift);
            forma::solveIk(doc.pool, chain, limb.tip, target);
        }
        if (swing) {
            forma::applyClip(doc.pool, doc.frames, *swing, time, forma::ClipBlend::Replace);
        }
    }
};

void drawDoc(sf::RenderTarget& target, const forma::CompiledDocument& doc) {
    for (const forma::CompiledPrimitive& primitive : doc.primitives) {
        if (primitive.skin && primitive.kind == forma::PrimitiveKind::Polygon) {
            const forma::Polyline2 line = forma::deformSkinLine(doc.pool, primitive.polygon.nodes, true);
            forma::Polygon2 shape;
            shape.outer.pts = line.pts;
            if (primitive.fill) {
                forma::draw(target, forma::fillPolygon(shape, *primitive.fill));
            }
            continue;
        }
        if (primitive.kind == forma::PrimitiveKind::Circle) {
            const forma::Polygon2 shape = forma::sample(doc.pool, primitive.circle, 0.8f);
            if (primitive.fill) {
                forma::draw(target, forma::fillPolygon(shape, *primitive.fill));
            }
        } else if (primitive.kind == forma::PrimitiveKind::Polygon) {
            const forma::Polygon2 shape = forma::resolve(doc.pool, primitive.polygon);
            if (primitive.fill) {
                forma::draw(target, forma::fillPolygon(shape, *primitive.fill));
            }
        }
    }
    for (const forma::Bone& bone : forma::bones(doc.pool)) {
        sf::VertexArray line(sf::PrimitiveType::Lines, 2);
        line[0] = {{bone.from.x, bone.from.y}, sf::Color(0x1c, 0x19, 0x15)};
        line[1] = {{bone.to.x, bone.to.y}, sf::Color(0x1c, 0x19, 0x15)};
        target.draw(line);
    }
}

}  // namespace

int main() {
    forma::Document humanDoc;
    forma::Document quadDoc;
    try {
        humanDoc = forma::documentFromJson(readFile("examples/human.json"));
        quadDoc = forma::documentFromJson(readFile("examples/quadruped.json"));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    Walker biped;
    biped.load("examples/human.json", {180.f, 280.f}, {{"R", 0.f}, {"L", 0.5f}});
    for (const forma::Clip& clip : humanDoc.clips) {
        if (clip.id == "swing") {
            biped.swing = &clip;
        }
    }
    Walker quadruped;
    quadruped.load("examples/quadruped.json", {680.f, 300.f},
                   {{"BL", 0.f}, {"FR", 0.f}, {"BR", 0.5f}, {"FL", 0.5f}});

    sf::RenderWindow window(sf::VideoMode({1100u, 640u}), "forma walk");
    window.setFramerateLimit(60);
    sf::Clock clock;
    float time = 0.f;
    while (window.isOpen()) {
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
            }
        }
        time += clock.restart().asSeconds();
        biped.tick(time, 22.f, 12.f);
        quadruped.tick(time, 28.f, 14.f);
        window.clear(sf::Color(0xe7, 0xdf, 0xd0));
        sf::RectangleShape ground({sf::Vector2f(1100.f, 4.f)});
        ground.setPosition({0.f, 520.f});
        ground.setFillColor(sf::Color(0x1c, 0x19, 0x15));
        window.draw(ground);
        drawDoc(window, biped.doc);
        drawDoc(window, quadruped.doc);
        window.display();
    }
    return 0;
}
