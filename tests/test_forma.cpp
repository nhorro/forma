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

void skeletonTests() {
    using namespace forma;
    const float pi = 3.1415926535f;
    NodePool pool;
    const NodeId joint = pool.create({40.f, 0.f});
    const FrameId bone = pool.createFrame(pool.root());
    pool.setPivot(bone, joint);
    CHECK(std::fabs(pool.frameWorld(bone).apply({0.f, 0.f}).x - 40.f) < 1e-3f);
    pool.place(joint, {55.f, 4.f});
    CHECK(std::fabs(pool.frameWorld(bone).apply({0.f, 0.f}).x - 55.f) < 1e-3f);
    CHECK(std::fabs(pool.frameWorld(bone).apply({0.f, 0.f}).y - 4.f) < 1e-3f);
    pool.set(joint, {10.f, 0.f});
    CHECK(std::fabs(pool.frameWorld(bone).apply({0.f, 0.f}).x - 10.f) < 1e-3f);
    CHECK(std::fabs(pool.frameBind(bone).apply({0.f, 0.f}).x - 55.f) < 1e-3f);

    pool.setLimits(bone, true, -0.2f, 0.4f);
    FramePose posed = pool.pose(bone);
    posed.rotation = 1.2f;
    pool.setPose(bone, posed);
    CHECK(std::fabs(pool.pose(bone).rotation - 1.2f) < 1e-4f);
    pool.clampToLimits(bone);
    CHECK(std::fabs(pool.pose(bone).rotation - 0.4f) < 1e-4f);
    const std::vector<Bone> rig = bones(pool);
    CHECK(rig.size() == 1);
    CHECK(std::fabs(rig[0].to.x - 10.f) < 1e-3f);

    bool rejected = false;
    try {
        pool.setPivot(pool.root(), joint);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);

    NodePool skin;
    const FrameId limb = skin.createFrame(skin.root());
    FramePose bent;
    bent.rotation = pi / 2.f;
    skin.setPose(limb, bent);
    const Vec2 rest{20.f, 0.f};
    const BoneInfluence influence{limb, 80.f};
    const std::vector<Vec2> deformed =
        deformSkin(skin, std::span<const Vec2>(&rest, 1), std::span<const BoneInfluence>(&influence, 1));
    CHECK(std::fabs(deformed[0].x) < 1e-2f);
    CHECK(std::fabs(deformed[0].y - 20.f) < 1e-2f);

    const char* json = R"({
      "forma": 1,
      "kind": "asset",
      "root": "root",
      "frames": [
        {"id": "root", "t": [0, 0], "r": 0, "s": [1, 1]},
        {"id": "knee", "parent": "root", "pivot": "j", "t": [10, 0], "r": 0, "s": [1, 1], "limit": [-10, 140]}
      ],
      "nodes": [{"id": "j", "frame": "root", "p": [10, 0]}],
      "primitives": []
    })";
    const Document doc = documentFromJson(json);
    const CompiledDocument compiled = compile(doc);
    const FrameId knee = compiled.frames.at("knee");
    CHECK(compiled.pool.hasLimits(knee));
    CHECK(std::fabs(compiled.pool.limitMin(knee) * (180.f / pi) + 10.f) < 1e-2f);
    CHECK(std::fabs(compiled.pool.limitMax(knee) * (180.f / pi) - 140.f) < 1e-2f);
    CHECK(std::fabs(compiled.pool.frameWorld(knee).apply({0.f, 0.f}).x - 10.f) < 1e-3f);
    const Document round = documentFromJson(toJson(doc));
    CHECK(round.frames[1].pivot == "j");
    CHECK(round.frames[1].hasLimit);
    CHECK(std::fabs(round.frames[1].limitMax * (180.f / pi) - 140.f) < 1e-2f);
}

void ikTests() {
    using namespace forma;
    const float pi = 3.1415926535f;
    NodePool pool;
    const NodeId shoulder = pool.create({0.f, 0.f});
    const FrameId upper = pool.createFrame(pool.root());
    pool.setPivot(upper, shoulder);
    const NodeId elbow = pool.create(upper, {40.f, 0.f});
    const FrameId lower = pool.createFrame(upper);
    pool.setPivot(lower, elbow);
    const FrameId chain[] = {upper, lower};
    const Vec2 tipLocal{40.f, 0.f};

    IkOptions down;
    down.pole = Vec2{0.f, 20.f};
    const IkResult reached = solveIk(pool, chain, tipLocal, {40.f, 0.f}, down);
    CHECK(reached.reached);
    CHECK(reached.error < 0.1f);
    const Vec2 kneeDown = pool.frameWorld(lower).apply({0.f, 0.f});
    CHECK(kneeDown.y > 1.f);

    IkOptions up;
    up.pole = Vec2{0.f, -20.f};
    const IkResult other = solveIk(pool, chain, tipLocal, {40.f, 0.f}, up);
    CHECK(other.reached);
    const Vec2 kneeUp = pool.frameWorld(lower).apply({0.f, 0.f});
    CHECK(kneeUp.y < -1.f);
    CHECK(std::fabs(pool.frameWorld(upper).apply({0.f, 0.f}).x) < 1e-3f);

    pool.setLimits(lower, true, 0.f, 10.f * pi / 180.f);
    const IkResult blocked = solveIk(pool, chain, tipLocal, {0.f, 70.f});
    CHECK(!blocked.reached);
    const float knee = pool.pose(lower).rotation - pool.restPose(lower).rotation;
    CHECK(knee >= -1e-3f);
    CHECK(knee <= 10.f * pi / 180.f + 1e-3f);

    NodePool tail;
    FrameId previous = tail.root();
    FrameId bones[4];
    for (int i = 0; i < 4; ++i) {
        const NodeId joint = tail.create(previous, {20.f, 0.f});
        bones[i] = tail.createFrame(previous);
        tail.setPivot(bones[i], joint);
        previous = bones[i];
    }
    const IkResult curled = solveIk(tail, bones, {20.f, 0.f}, {30.f, 40.f});
    CHECK(curled.reached);
    CHECK(std::fabs(tail.frameWorld(bones[0]).apply({0.f, 0.f}).x - 20.f) < 1e-2f);
    for (FrameId bone : bones) {
        tail.setLimits(bone, true, -40.f * pi / 180.f, 40.f * pi / 180.f);
    }
    tail.resetToRest();
    const IkResult limited = solveIk(tail, bones, {20.f, 0.f}, {30.f, 40.f});
    CHECK(!limited.reached);
    for (FrameId bone : bones) {
        const float rel = tail.pose(bone).rotation - tail.restPose(bone).rotation;
        CHECK(rel >= -40.f * pi / 180.f - 1e-3f);
        CHECK(rel <= 40.f * pi / 180.f + 1e-3f);
    }
}

void backlogTests() {
    using namespace forma;
    const float pi = 3.1415926535f;
    const Vec2 ell[] = {{0.f, 0.f}, {30.f, 0.f}, {30.f, 10.f}, {10.f, 10.f}, {10.f, 30.f}, {0.f, 30.f}};
    const std::vector<std::vector<Vec2>> parts = convexParts(ell);
    CHECK(parts.size() >= 2);
    float area = 0.f;
    for (const std::vector<Vec2>& part : parts) {
        CHECK(part.size() >= 3);
        CHECK(static_cast<int>(part.size()) <= 8);
        CHECK(isConvex(part));
        area += std::fabs(signedArea(part));
    }
    CHECK(std::fabs(area - 500.f) < 2.f);
    bool notch = false;
    for (const std::vector<Vec2>& part : parts) {
        if (pointInPolygon({20.f, 20.f}, part)) {
            notch = true;
        }
    }
    CHECK(!notch);

    const Vec2 box[] = {{0.f, 0.f}, {0.f, 10.f}, {10.f, 10.f}, {10.f, 0.f}};
    CHECK(convexParts(box).size() == 1);
    PhysicsScale scale;
    const std::vector<ConvexBody> fixtures = buildConvexParts(ell, scale);
    CHECK(fixtures.size() >= 2);
    for (const ConvexBody& fixture : fixtures) {
        CHECK(fixture.ok);
    }

    std::vector<Vec2> wheel;
    for (int i = 0; i < 12; ++i) {
        const float a = -static_cast<float>(i) * (2.f * pi / 12.f);
        wheel.push_back({std::cos(a) * 40.f, std::sin(a) * 40.f});
    }
    const std::vector<std::vector<Vec2>> wheelParts = convexParts(wheel);
    float wheelArea = 0.f;
    CHECK(wheelParts.size() >= 2);
    for (const std::vector<Vec2>& part : wheelParts) {
        CHECK(static_cast<int>(part.size()) <= 8);
        CHECK(isConvex(part));
        wheelArea += std::fabs(signedArea(part));
    }
    CHECK(std::fabs(wheelArea - std::fabs(signedArea(wheel))) < 2.f);

    const Vec2 level[] = {{0.f, 0.f}, {240.f, 0.f}, {240.f, 80.f}, {80.f, 80.f}, {80.f, 240.f}, {0.f, 240.f}};
    b2WorldDef levelWorldDef = b2DefaultWorldDef();
    levelWorldDef.gravity = {0.f, -10.f};
    const b2WorldId levelWorld = b2CreateWorld(&levelWorldDef);
    b2BodyDef groundDef = b2DefaultBodyDef();
    groundDef.type = b2_staticBody;
    const b2BodyId ground = b2CreateBody(levelWorld, &groundDef);
    CHECK(attachConvexParts(ground, level, scale) >= 2);
    auto drop = [&](Vec2 at) {
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_dynamicBody;
        bodyDef.position = toWorld(at, scale);
        const b2BodyId body = b2CreateBody(levelWorld, &bodyDef);
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = 1.f;
        const b2Circle circle = buildCircle(10.f, scale);
        b2CreateCircleShape(body, &shapeDef, &circle);
        return body;
    };
    const b2BodyId inNotch = drop({160.f, 160.f});
    const b2BodyId onSolid = drop({160.f, -40.f});
    for (int step = 0; step < 180; ++step) {
        b2World_Step(levelWorld, 1.f / 60.f, 4);
    }
    const Vec2 notchAt = toScreen(b2Body_GetPosition(inNotch), scale);
    const Vec2 solidAt = toScreen(b2Body_GetPosition(onSolid), scale);
    CHECK(notchAt.y > 280.f);
    CHECK(std::fabs(solidAt.y + 10.f) < 8.f);
    b2DestroyWorld(levelWorld);

    NodePool pool;
    const FrameId bone = pool.createFrame(pool.root());
    pool.setPivot(bone, pool.create({40.f, 0.f}));
    pool.setInfluence(bone, 100.f);
    const NodeId skinPoint = pool.create({70.f, 0.f});
    FramePose posed = pool.pose(bone);
    posed.rotation = pi / 2.f;
    pool.setPose(bone, posed);
    const Polyline2 skinned = deformSkinLine(pool, std::span<const NodeId>(&skinPoint, 1), false);
    CHECK(std::fabs(skinned.pts[0].x - 40.f) < 1.5f);
    CHECK(std::fabs(skinned.pts[0].y - 30.f) < 1.5f);

    Clip clip;
    clip.duration = 1.f;
    ClipTrack track;
    track.frame = "bone";
    track.keys = {{0.f, 0.f}, {0.5f, 0.4f}, {1.f, 0.f}};
    clip.tracks.push_back(track);
    CHECK(std::fabs(sampleClip(clip, track, 0.25f) - 0.2f) < 1e-3f);
    track.keys = {{0.5f, 0.4f}, {0.f, 0.f}, {1.f, 0.f}};
    CHECK(std::fabs(sampleClip(clip, track, 0.25f) - 0.2f) < 1e-3f);
    track.keys = {{0.f, 0.f}, {0.5f, 1.f}};
    CHECK(std::fabs(sampleClip(clip, track, 0.75f) - 0.5f) < 1e-3f);
    CHECK(std::fabs(sampleClip(clip, track, 1.75f) - 0.5f) < 1e-3f);
    const std::unordered_map<std::string, FrameId> names{{"bone", bone}};
    pool.resetToRest();
    applyClip(pool, names, clip, 0.5f, ClipBlend::Replace);
    CHECK(std::fabs(pool.pose(bone).rotation - 0.4f) < 1e-3f);
    pool.setLimits(bone, true, -0.1f, 0.1f);
    applyClip(pool, names, clip, 0.5f, ClipBlend::Replace);
    CHECK(std::fabs(pool.pose(bone).rotation - 0.1f) < 1e-3f);
    pool.setLimits(bone, false, 0.f, 0.f);

    const char* json = R"({
      "forma": 1, "kind": "asset", "root": "root",
      "frames": [{"id": "root", "t": [0, 0], "r": 0, "s": [1, 1]}],
      "clips": [{"id": "wave", "duration": 1, "tracks": [{"frame": "root", "keys": [{"t": 0, "r": 0}, {"t": 1, "r": 30}]}]}],
      "primitives": [{"id": "skin", "kind": "polyline", "skin": true}]
    })";
    const Document round = documentFromJson(toJson(documentFromJson(json)));
    CHECK(round.clips.size() == 1);
    CHECK(std::fabs(round.clips[0].tracks[0].keys[1].rotation * (180.f / pi) - 30.f) < 1e-2f);
    CHECK(round.primitives[0].skin);

    NodePool limb;
    const FrameId upper = limb.createFrame(limb.root());
    limb.setPivot(upper, limb.create({0.f, 0.f}));
    const FrameId lower = limb.createFrame(upper);
    limb.setPivot(lower, limb.create(upper, {50.f, 0.f}));
    limb.setLimits(lower, true, 0.f, 0.6f);
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = {0.f, -10.f};
    const b2WorldId world = b2CreateWorld(&worldDef);
    RagdollOptions options;
    options.pinRoot = true;
    const Ragdoll ragdoll = createRagdoll(world, limb, scale, options);
    CHECK(ragdoll.joints.size() == 2);
    b2JointId hinge = b2_nullJointId;
    for (const HingeJoint& joint : ragdoll.joints) {
        if (joint.child == lower) {
            hinge = joint.id;
        }
    }
    CHECK(b2Joint_IsValid(hinge));
    for (int step = 0; step < 90; ++step) {
        b2World_Step(world, 1.f / 60.f, 4);
    }
    const float angle = b2RevoluteJoint_GetAngle(hinge);
    CHECK(angle >= b2RevoluteJoint_GetLowerLimit(hinge) - 0.08f);
    CHECK(angle <= b2RevoluteJoint_GetUpperLimit(hinge) + 0.08f);
    b2DestroyWorld(world);
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
    skeletonTests();
    ikTests();
    backlogTests();

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
