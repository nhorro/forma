#include "forma/box2d_export.hpp"
#include "forma/forma.hpp"

#include <cmath>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>

namespace {

int g_fails = 0;

void check(bool cond, const char* expr, int line) {
    if (!cond) {
        std::cerr << "FAIL " << line << ": " << expr << '\n';
        ++g_fails;
    }
}

#define CHECK(cond) check(static_cast<bool>(cond), #cond, __LINE__)

forma::Polygon2 square(float x, float y, float s) {
    forma::Polygon2 poly;
    // Screen-space counter-clockwise: down, right, up.
    poly.outer.pts = {{x, y}, {x, y + s}, {x + s, y + s}, {x + s, y}};
    return poly;
}

float totalArea(const std::vector<forma::Polygon2>& shapes) {
    float area = 0.f;
    for (const forma::Polygon2& shape : shapes) {
        area += forma::signedArea(shape.outer.pts);
        for (const forma::Contour& hole : shape.holes) {
            area += forma::signedArea(hole.pts);
        }
    }
    return area;
}

bool threwInvalid(const auto& fn) {
    try {
        fn();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

void hierarchyTests() {
    using namespace forma;
    const float pi = 3.1415926535f;

    {
        NodePool pool;
        FramePose squash;
        squash.scale = {2.f, 1.f};
        pool.placePose(pool.root(), squash);
        const FrameId eye = pool.createFrame(pool.root());
        FramePose tilt;
        tilt.rotation = pi / 2.f;
        pool.placePose(eye, tilt);
        const NodeId node = pool.create(eye, {1.f, 0.f});
        const Vec2 world = pool.worldPosition(node);
        // Parent scale applies in the parent axes, after the child's rotation.
        CHECK(std::fabs(world.x) < 1e-3f);
        CHECK(std::fabs(world.y - 1.f) < 1e-3f);
    }

    {
        NodePool pool;
        FramePose face;
        face.rotation = pi / 2.f;
        pool.placePose(pool.root(), face);
        const FrameId eye = pool.createFrame(pool.root());
        pool.setInherit(eye, true, false, true);
        FramePose place;
        place.translation = {10.f, 0.f};
        pool.placePose(eye, place);
        const NodeId pupil = pool.create(eye, {3.f, 0.f});
        const Vec2 world = pool.worldPosition(pupil);
        CHECK(std::fabs(world.x - 3.f) < 1e-3f);
        CHECK(std::fabs(world.y - 10.f) < 1e-3f);
        const uint64_t before = stamp(pool, std::span<const NodeId>(&pupil, 1));
        FramePose nudged = pool.pose(pool.root());
        nudged.translation = {4.f, 0.f};
        pool.setPose(pool.root(), nudged);
        CHECK(stamp(pool, std::span<const NodeId>(&pupil, 1)) != before);
    }

    {
        NodePool pool;
        const FrameId child = pool.createFrame(pool.root());
        FramePose pose;
        pose.translation = {30.f, 40.f};
        pool.placePose(child, pose);
        const NodeId center = pool.create(child, {0.f, 0.f});
        const Polygon2 disk = sample(pool, Circle{center, 10.f}, 0.5f);
        for (Vec2 p : disk.outer.pts) {
            CHECK(std::fabs(distance(p, {30.f, 40.f}) - 10.f) < 0.05f);
        }
        const FrameId other = pool.createFrame(pool.root());
        const NodeId a = pool.create(child, {0.f, 0.f});
        const NodeId b = pool.create(other, {5.f, 0.f});
        Polyline line;
        line.nodes = {a, b};
        CHECK(threwInvalid([&] { resolve(pool, line); }));
        const FrameId grand = pool.createFrame(child);
        CHECK(threwInvalid([&] { pool.reparent(child, grand); }));
        const Vec2 back = pool.toLocal(child, pool.frameWorld(child).apply({3.f, -2.f}));
        CHECK(std::fabs(back.x - 3.f) < 1e-3f);
        CHECK(std::fabs(back.y + 2.f) < 1e-3f);
    }

    const char* json = R"({
      "forma": 1,
      "kind": "asset",
      "name": "face",
      "root": "face",
      "frames": [
        {"id": "face", "t": [0, 0], "r": 90, "s": [1, 1]},
        {"id": "eye", "parent": "face", "t": [10, 0], "r": 0, "s": [1, 1],
         "inherit": {"t": true, "r": false, "s": true}}
      ],
      "nodes": [{"id": "pupil", "frame": "eye", "p": [3, 0]}],
      "primitives": [
        {"id": "pupil.fill", "kind": "circle", "center": "pupil", "radius": 2, "fill": "#1c1915"}
      ]
    })";
    Document face = documentFromJson(json);
    CompiledDocument compiled = compile(face);
    const Vec2 pupil = compiled.pool.worldPosition(compiled.nodes.at("pupil"));
    CHECK(std::fabs(pupil.x - 3.f) < 1e-2f);
    CHECK(std::fabs(pupil.y - 10.f) < 1e-2f);
    CHECK(!compiled.pool.inheritsRotation(compiled.frames.at("eye")));
    compiled.pool.place(compiled.nodes.at("pupil"), {8.f, 1.f});
    writePose(face, compiled);
    CHECK(face.nodes[0].position.x == 8.f);
    const Document round = documentFromJson(toJson(face));
    const CompiledDocument again = compile(round);
    CHECK(!again.pool.inheritsRotation(again.frames.at("eye")));
    const Vec2 saved = again.pool.worldPosition(again.nodes.at("pupil"));
    CHECK(std::fabs(saved.x - 8.f) < 1e-2f);
    CHECK(std::fabs(saved.y - 11.f) < 1e-2f);

    Document asset;
    asset.kind = DocumentKind::Asset;
    asset.root = "body";
    asset.frames.push_back(FrameDesc{"body", "", {}, true, true, true});
    asset.nodes.push_back(NodeDesc{"c", "body", {2.f, 0.f}});
    Document level;
    level.kind = DocumentKind::World;
    level.root = "root";
    level.frames.push_back(FrameDesc{"root", "", {}, true, true, true});
    InstanceDesc mob;
    mob.id = "mob";
    mob.asset = "mob.json";
    mob.parent = "root";
    mob.pose.translation = {100.f, 40.f};
    level.instances.push_back(mob);
    const CompiledDocument placed = compile(level, [&](std::string_view) { return asset; });
    const Vec2 grafted = placed.pool.worldPosition(placed.nodes.at("mob/c"));
    CHECK(std::fabs(grafted.x - 102.f) < 1e-3f);
    CHECK(std::fabs(grafted.y - 40.f) < 1e-3f);

    bool bad = false;
    try {
        documentFromJson("{");
    } catch (const std::runtime_error&) {
        bad = true;
    }
    CHECK(bad);
}

}  // namespace

int main() {
    using namespace forma;

    const Polygon2 unit = square(0.f, 0.f, 100.f);
    CHECK(std::fabs(signedArea(unit.outer.pts) - 10000.f) < 1.f);
    CHECK(isScreenCounterClockwise(unit.outer.pts));
    CHECK(pointInPolygon({50.f, 50.f}, unit.outer.pts));
    CHECK(!pointInPolygon({-1.f, 50.f}, unit.outer.pts));
    CHECK(segmentsIntersect({0.f, 0.f}, {10.f, 10.f}, {0.f, 10.f}, {10.f, 0.f}));
    CHECK(!segmentsIntersect({0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}, {1.f, 1.f}));
    CHECK(isConvex(unit.outer.pts));

    NodePool pool;
    const NodeId a = pool.create({0.f, 0.f});
    const NodeId b = pool.create({100.f, 0.f});
    const NodeId c = pool.create({100.f, 40.f});
    const NodeId d = pool.create({0.f, 80.f});
    CHECK(pool.generation(b) == 1);
    pool.set(b, {120.f, 10.f});
    CHECK(pool.generation(b) == 2);
    CHECK(pool.get(b).x == 120.f);

    CatmullRom curve;
    curve.nodes = {a, b, c, d};
    curve.parameterization = CurveParameterization::Centripetal;
    const Polyline2 sampled = sample(pool, curve, 0.5f);
    CHECK(sampled.pts.size() > 4);
    // The spline passes through its control points.
    auto nearPoint = [&](Vec2 p) {
        float best = 1e9f;
        for (Vec2 q : sampled.pts) {
            best = std::min(best, distance(p, q));
        }
        return best;
    };
    CHECK(nearPoint(pool.get(a)) < 0.6f);
    CHECK(nearPoint(pool.get(b)) < 0.6f);
    CHECK(nearPoint(pool.get(c)) < 0.6f);
    CHECK(nearPoint(pool.get(d)) < 0.6f);

    const NodeId center = pool.create({0.f, 0.f});
    const Polygon2 disk = sample(pool, Circle{center, 50.f}, 0.75f);
    CHECK(disk.outer.pts.size() >= 12);
    CHECK(std::fabs(signedArea(disk.outer.pts) - 3.14159f * 50.f * 50.f) < 200.f);
    for (Vec2 p : disk.outer.pts) {
        CHECK(std::fabs(distance(p, {0.f, 0.f}) - 50.f) < 0.05f);
    }

    const std::vector<Vec2> hull = convexHull({{0.f, 0.f}, {0.f, 10.f}, {5.f, 5.f}, {10.f, 10.f}, {10.f, 0.f}});
    CHECK(hull.size() == 4);

    const Polygon2 left = square(0.f, 0.f, 100.f);
    const Polygon2 right = square(50.f, 0.f, 100.f);
    const Polygon2 parts[] = {left, right};
    const std::vector<Polygon2> merged = unite(parts);
    const float mergedArea = totalArea(merged);
    CHECK(std::fabs(mergedArea - 15000.f) < 5.f);

    const std::vector<Polygon2> grown = inflate(std::span<const Polygon2>(&left, 1), 10.f);
    CHECK(totalArea(grown) > 14000.f);

    const TriMesh fill = triangulate(std::span<const Polygon2>(&left, 1), Color::hex(0xffffff));
    CHECK(fill.vertices.size() >= 3);
    CHECK(fill.vertices.size() % 3 == 0);

    StrokeStyle stroke;
    stroke.width = 4.f;
    Polyline2 segment;
    segment.pts = {{0.f, 0.f}, {100.f, 0.f}};
    const TriMesh ribbon = strokePolyline(segment, stroke);
    CHECK(ribbon.vertices.size() >= 6);

    hierarchyTests();

    // Box2D: a circle dropped onto a ground chain comes to rest on it.
    PhysicsScale scale;
    scale.pixelsPerMeter = 32.f;
    scale.flipY = true;
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = {0.f, -10.f};
    const b2WorldId world = b2CreateWorld(&worldDef);

    b2BodyDef groundDef = b2DefaultBodyDef();
    groundDef.type = b2_staticBody;
    const b2BodyId ground = b2CreateBody(world, &groundDef);
    Polyline2 terrain;
    terrain.pts = {{0.f, 400.f}, {200.f, 400.f}, {400.f, 400.f}, {600.f, 400.f}};
    const ChainPoints chainPts = buildChainPoints(terrain, scale, ChainFront::PositiveY, 40.f);
    CHECK(chainPts.points.size() >= 4);
    const b2ChainId chain = createChain(ground, chainPts);
    CHECK(b2Chain_IsValid(chain));

    b2BodyDef ballDef = b2DefaultBodyDef();
    ballDef.type = b2_dynamicBody;
    ballDef.position = toWorld({300.f, 100.f}, scale);
    const b2BodyId ball = b2CreateBody(world, &ballDef);
    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = 1.f;
    shapeDef.material.restitution = 0.05f;
    const b2Circle circle = buildCircle(20.f, scale);
    b2CreateCircleShape(ball, &shapeDef, &circle);

    for (int i = 0; i < 300; ++i) {
        b2World_Step(world, 1.f / 60.f, 4);
    }
    const Vec2 landed = toScreen(b2Body_GetPosition(ball), scale);
    // Ground is y=400, radius 20, so the center should sit near y=380, not fall through.
    CHECK(landed.y > 340.f);
    CHECK(landed.y < 400.f);
    if (!(landed.y > 340.f && landed.y < 400.f)) {
        std::cerr << "  ball landed at " << landed.x << ", " << landed.y << '\n';
    }

    const std::vector<Vec2> gem = {{0.f, 0.f}, {40.f, 10.f}, {30.f, 50.f}, {-10.f, 40.f}};
    const ConvexBody convex = buildConvexBody(gem, scale);
    CHECK(convex.ok);
    CHECK(convex.local.size() == gem.size());

    b2DestroyWorld(world);

    if (g_fails != 0) {
        std::cerr << g_fails << " check(s) failed\n";
        return 1;
    }
    std::cout << "forma tests passed\n";
    return 0;
}
