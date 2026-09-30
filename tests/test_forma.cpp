#include "forma/box2d_export.hpp"
#include "forma/forma.hpp"

#include <cmath>
#include <iostream>
#include <span>
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
