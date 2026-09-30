#include "forma/box2d_export.hpp"
#include "forma/forma.hpp"
#include "forma/sfml_draw.hpp"

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace {

constexpr unsigned kWidth = 1100;
constexpr unsigned kHeight = 700;

struct Rigid {
    b2BodyId body = b2_nullBodyId;
    std::vector<forma::NodeId> nodes;
    std::vector<b2Vec2> local;
    bool circle = false;
    float radius = 0.f;
    std::vector<forma::Vec2> spawnPoints;
};

struct App {
    forma::NodePool pool;
    forma::PhysicsScale scale{};
    b2WorldId world = b2_nullWorldId;
    b2BodyId ground = b2_nullBodyId;
    b2ChainId chain = b2_nullChainId;
    uint64_t chainStamp = 0;

    std::vector<forma::NodeId> terrain;
    std::vector<forma::NodeId> spline;
    std::vector<forma::NodeId> blob;
    forma::NodeId ornament{};
    float ornamentRadius = 76.f;
    forma::NodeId shared{};

    std::vector<Rigid> rigids;
    std::optional<forma::NodeId> drag;
    forma::Vec2 lastMouse{};
};

forma::NodeId add(App& app, forma::Vec2 p) { return app.pool.create(p); }

int rigidIndex(const App& app, forma::NodeId id) {
    for (int i = 0; i < static_cast<int>(app.rigids.size()); ++i) {
        const auto& nodes = app.rigids[static_cast<std::size_t>(i)].nodes;
        if (std::find(nodes.begin(), nodes.end(), id) != nodes.end()) {
            return i;
        }
    }
    return -1;
}

void syncRigid(App& app, Rigid& rigid) {
    const b2Vec2 position = b2Body_GetPosition(rigid.body);
    const b2Rot rotation = b2Body_GetRotation(rigid.body);
    for (std::size_t i = 0; i < rigid.nodes.size(); ++i) {
        const b2Vec2 local = rigid.local[i];
        const b2Vec2 world{position.x + rotation.c * local.x - rotation.s * local.y,
                           position.y + rotation.s * local.x + rotation.c * local.y};
        app.pool.set(rigid.nodes[i], forma::toScreen(world, app.scale));
    }
}

void grabRigid(App& app, Rigid& rigid, forma::NodeId grabbed, forma::Vec2 mouse) {
    const b2Rot rotation = b2Body_GetRotation(rigid.body);
    std::size_t index = 0;
    for (; index < rigid.nodes.size(); ++index) {
        if (rigid.nodes[index] == grabbed) {
            break;
        }
    }
    const b2Vec2 local = rigid.local[index];
    const b2Vec2 rotated{rotation.c * local.x - rotation.s * local.y, rotation.s * local.x + rotation.c * local.y};
    const b2Vec2 target = forma::toWorld(mouse, app.scale);
    const b2Vec2 position{target.x - rotated.x, target.y - rotated.y};
    b2Body_SetTransform(rigid.body, position, rotation);
    b2Body_SetLinearVelocity(rigid.body, {0.f, 0.f});
    b2Body_SetAngularVelocity(rigid.body, 0.f);
    b2Body_SetAwake(rigid.body, true);
    syncRigid(app, rigid);
}

void rebuildChain(App& app) {
    if (b2Chain_IsValid(app.chain)) {
        b2DestroyChain(app.chain);
        app.chain = b2_nullChainId;
    }
    forma::Polyline line;
    line.nodes = app.terrain;
    const forma::Polyline2 resolved = forma::resolve(app.pool, line);
    const forma::ChainPoints points = forma::buildChainPoints(resolved, app.scale, forma::ChainFront::PositiveY, 90.f);
    app.chain = forma::createChain(app.ground, points);
    app.chainStamp = forma::stamp(app.pool, app.terrain);
}

b2BodyId makeCircleBody(App& app, forma::Vec2 center, float radius) {
    b2BodyDef def = b2DefaultBodyDef();
    def.type = b2_dynamicBody;
    def.position = forma::toWorld(center, app.scale);
    const b2BodyId body = b2CreateBody(app.world, &def);
    b2ShapeDef shape = b2DefaultShapeDef();
    shape.density = 1.2f;
    shape.material.friction = 0.45f;
    shape.material.restitution = 0.22f;
    const b2Circle circle = forma::buildCircle(radius, app.scale);
    b2CreateCircleShape(body, &shape, &circle);
    return body;
}

b2BodyId makePolygonBody(App& app, const forma::ConvexBody& convex) {
    b2BodyDef def = b2DefaultBodyDef();
    def.type = b2_dynamicBody;
    def.position = convex.position;
    const b2BodyId body = b2CreateBody(app.world, &def);
    b2ShapeDef shape = b2DefaultShapeDef();
    shape.density = 1.f;
    shape.material.friction = 0.5f;
    shape.material.restitution = 0.12f;
    b2CreatePolygonShape(body, &shape, &convex.polygon);
    return body;
}

void spawnRigids(App& app) {
    for (Rigid& rigid : app.rigids) {
        if (b2Body_IsValid(rigid.body)) {
            b2DestroyBody(rigid.body);
            rigid.body = b2_nullBodyId;
        }
    }
    app.rigids.clear();

    {
        Rigid ball;
        ball.circle = true;
        ball.radius = 26.f;
        ball.spawnPoints = {{340.f, 150.f}};
        ball.nodes = {add(app, ball.spawnPoints.front())};
        ball.local = {{0.f, 0.f}};
        ball.body = makeCircleBody(app, ball.spawnPoints.front(), ball.radius);
        app.rigids.push_back(std::move(ball));
    }
    {
        const forma::Vec2 center{780.f, 170.f};
        const forma::Vec2 offsets[] = {{0.f, -48.f}, {44.f, -12.f}, {28.f, 40.f}, {-30.f, 42.f}, {-46.f, -8.f}};
        std::vector<forma::Vec2> points;
        Rigid gem;
        for (forma::Vec2 offset : offsets) {
            points.push_back(center + offset);
            gem.nodes.push_back(add(app, points.back()));
        }
        gem.spawnPoints = points;
        const forma::ConvexBody convex = forma::buildConvexBody(points, app.scale);
        gem.local = convex.local;
        gem.body = makePolygonBody(app, convex);
        app.rigids.push_back(std::move(gem));
    }
}

void resetRigids(App& app) {
    // Recreate from the stored spawn, leaving the authored nodes in place.
    for (Rigid& rigid : app.rigids) {
        if (b2Body_IsValid(rigid.body)) {
            b2DestroyBody(rigid.body);
        }
        if (rigid.circle) {
            rigid.body = makeCircleBody(app, rigid.spawnPoints.front(), rigid.radius);
            rigid.local = {{0.f, 0.f}};
        } else {
            const forma::ConvexBody convex = forma::buildConvexBody(rigid.spawnPoints, app.scale);
            rigid.local = convex.local;
            rigid.body = makePolygonBody(app, convex);
        }
        syncRigid(app, rigid);
    }
}

App makeApp() {
    App app;
    app.scale.pixelsPerMeter = 32.f;
    app.scale.flipY = true;

    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = {0.f, -10.f};
    app.world = b2CreateWorld(&worldDef);

    b2BodyDef groundDef = b2DefaultBodyDef();
    groundDef.type = b2_staticBody;
    app.ground = b2CreateBody(app.world, &groundDef);

    const forma::Vec2 ground[] = {{30.f, 590.f},  {160.f, 545.f}, {300.f, 585.f}, {450.f, 510.f},
                                  {610.f, 560.f}, {760.f, 500.f}, {920.f, 545.f}, {1070.f, 590.f}};
    for (forma::Vec2 p : ground) {
        app.terrain.push_back(add(app, p));
    }
    rebuildChain(app);

    const forma::Vec2 curve[] = {{110.f, 210.f}, {270.f, 145.f}, {450.f, 255.f}, {660.f, 155.f}, {880.f, 230.f}};
    for (forma::Vec2 p : curve) {
        app.spline.push_back(add(app, p));
    }
    app.shared = app.spline[2];
    app.blob = {app.shared, add(app, {390.f, 360.f}), add(app, {560.f, 390.f}), add(app, {630.f, 275.f})};
    app.ornament = add(app, {700.f, 300.f});

    spawnRigids(app);
    return app;
}

std::optional<forma::NodeId> pick(const App& app, forma::Vec2 mouse) {
    std::optional<forma::NodeId> best;
    float bestDistance = 16.f;
    for (uint32_t i = 0; i < app.pool.size(); ++i) {
        const forma::NodeId id{i};
        const float d = forma::distance(mouse, app.pool.get(id));
        if (d <= bestDistance) {
            bestDistance = d;
            best = id;
        }
    }
    return best;
}

forma::TriMesh handle(forma::Vec2 p, float radius, forma::Color color) {
    forma::NodePool scratch;
    const forma::NodeId id = scratch.create(p);
    const forma::Polygon2 disk = forma::sample(scratch, forma::Circle{id, radius}, 0.4f);
    return forma::fillConvex(disk.outer.pts, color);
}

void drawHandles(sf::RenderTarget& target, const App& app) {
    for (uint32_t i = 0; i < app.pool.size(); ++i) {
        const forma::NodeId id{i};
        const bool shared = id == app.shared;
        const forma::Color color = shared ? forma::Color::hex(0xff5a36) : forma::Color::hex(0xf4efe6);
        forma::draw(target, handle(app.pool.get(id), shared ? 8.f : 5.5f, color));
    }
}

}  // namespace

int main() {
    sf::RenderWindow window(sf::VideoMode({kWidth, kHeight}), "forma — drag nodes, Space pops the ball, R resets");
    window.setFramerateLimit(60);

    App app = makeApp();
    sf::Clock clock;
    float time = 0.f;
    float accumulator = 0.f;

    while (window.isOpen()) {
        const float frame = std::min(clock.restart().asSeconds(), 0.05f);
        time += frame;

        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
            } else if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
                if (key->code == sf::Keyboard::Key::Escape) {
                    window.close();
                } else if (key->code == sf::Keyboard::Key::R) {
                    resetRigids(app);
                } else if (key->code == sf::Keyboard::Key::Space && !app.rigids.empty()) {
                    b2Body_SetLinearVelocity(app.rigids.front().body, {1.5f, 9.f});
                    b2Body_SetAwake(app.rigids.front().body, true);
                }
            } else if (const auto* pressed = event->getIf<sf::Event::MouseButtonPressed>()) {
                if (pressed->button == sf::Mouse::Button::Left) {
                    const forma::Vec2 mouse{static_cast<float>(pressed->position.x),
                                            static_cast<float>(pressed->position.y)};
                    app.drag = pick(app, mouse);
                    app.lastMouse = mouse;
                }
            } else if (event->is<sf::Event::MouseButtonReleased>()) {
                if (app.drag && *app.drag == app.shared) {
                    app.pool.setRest(app.shared, app.pool.get(app.shared));
                }
                app.drag.reset();
            } else if (const auto* moved = event->getIf<sf::Event::MouseMoved>()) {
                if (app.drag) {
                    const forma::Vec2 mouse{static_cast<float>(moved->position.x),
                                            static_cast<float>(moved->position.y)};
                    app.lastMouse = mouse;
                    const int rigid = rigidIndex(app, *app.drag);
                    if (rigid >= 0) {
                        grabRigid(app, app.rigids[static_cast<std::size_t>(rigid)], *app.drag, mouse);
                    } else {
                        app.pool.place(*app.drag, mouse);
                    }
                }
            } else if (const auto* resized = event->getIf<sf::Event::Resized>()) {
                sf::View view;
                view.setSize({static_cast<float>(resized->size.x), static_cast<float>(resized->size.y)});
                view.setCenter(view.getSize() * 0.5f);
                window.setView(view);
            }
        }

        if (!(app.drag && *app.drag == app.shared)) {
            const forma::Vec2 rest = app.pool.rest(app.shared);
            app.pool.set(app.shared, {rest.x, rest.y + std::sin(time * 1.5f) * 34.f});
        }

        accumulator += frame;
        while (accumulator >= 1.f / 60.f) {
            b2World_Step(app.world, 1.f / 60.f, 4);
            accumulator -= 1.f / 60.f;
        }
        for (Rigid& rigid : app.rigids) {
            if (app.drag && std::find(rigid.nodes.begin(), rigid.nodes.end(), *app.drag) != rigid.nodes.end()) {
                grabRigid(app, rigid, *app.drag, app.lastMouse);
            } else {
                syncRigid(app, rigid);
            }
        }
        if (forma::stamp(app.pool, app.terrain) != app.chainStamp) {
            rebuildChain(app);
        }

        forma::CatmullRom curve;
        curve.nodes = app.spline;
        curve.parameterization = forma::CurveParameterization::Centripetal;
        const forma::Polyline2 spline = forma::sample(app.pool, curve, 0.6f);

        forma::Polygon blob;
        blob.nodes = app.blob;
        const forma::Polygon2 blobPoly = forma::resolve(app.pool, blob);
        const forma::Polygon2 ornament =
            forma::sample(app.pool, forma::Circle{app.ornament, app.ornamentRadius}, 0.6f);
        const forma::Polygon2 parts[] = {blobPoly, ornament};
        const std::vector<forma::Polygon2> merged = forma::unite(parts);

        forma::Polyline ground;
        ground.nodes = app.terrain;
        const forma::Polyline2 groundLine = forma::resolve(app.pool, ground);

        forma::StrokeStyle splineStroke;
        splineStroke.width = 3.5f;
        splineStroke.color = forma::Color::hex(0xff5a36);
        forma::StrokeStyle groundStroke;
        groundStroke.width = 3.f;
        groundStroke.color = forma::Color::hex(0xe7d7b8);
        groundStroke.cap = forma::LineCap::Butt;
        forma::StrokeStyle unionStroke;
        unionStroke.width = 1.5f;
        unionStroke.color = forma::Color::hex(0xd7f3e8);

        window.clear(sf::Color(18, 17, 15));
        forma::draw(window, forma::triangulate(merged, forma::Color::hex(0x1f6f5b, 210)));
        for (const forma::Polygon2& shape : merged) {
            forma::Polyline2 outline;
            outline.closed = true;
            outline.pts = shape.outer.pts;
            forma::draw(window, forma::strokePolyline(outline, unionStroke));
        }
        forma::draw(window, forma::strokePolyline(groundLine, groundStroke));
        forma::draw(window, forma::strokePolyline(spline, splineStroke));

        for (const Rigid& rigid : app.rigids) {
            if (rigid.circle) {
                const forma::Polygon2 disk =
                    forma::sample(app.pool, forma::Circle{rigid.nodes.front(), rigid.radius}, 0.45f);
                forma::draw(window, forma::fillConvex(disk.outer.pts, forma::Color::hex(0xf2c14e)));
            } else {
                forma::Polygon poly;
                poly.nodes = rigid.nodes;
                const forma::Polygon2 shape = forma::resolve(app.pool, poly);
                forma::draw(window, forma::fillConvex(shape.outer.pts, forma::Color::hex(0xf2c14e)));
            }
        }
        drawHandles(window, app);
        window.display();
    }

    b2DestroyWorld(app.world);
    return 0;
}
