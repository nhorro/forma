# forma

A small C++20 geometry kernel for procedural 2D animation. Nodes are shared. Shapes and curves store ids, not copies. Sampling turns them into polylines and polygons, and that baked form is what Clipper2 and Box2D see.

SFML and Box2D are adapters. They are not the model.

## Pieces

| Layer | What it is |
|---|---|
| `NodePool` | Local positions with stable `NodeId`s, each owned by one frame. |
| `Frame` | Parent transform. Children are frames, not shapes. Sample in local space, then transform. |
| Paths | `Polyline`, centripetal `CatmullRom` |
| Fills | `Polygon` (screen-space counter-clockwise), `Circle` |
| `Document` | JSON (`forma` 1) for creatures and levels. Degrees on disk, radians in the API. |
| Joint | Optional pivot node on a child frame, plus a hinge limit relative to the rest angle. |
| `deformSkin` | Soft bake from bone influence radii. Parented shapes do not use it. |
| `solveIk` | One limb. Two-bone chains are exact, with a pole so the knee stays put. Longer chains use FABRIK. |
| `Clip` | Joint angles over time, relative to rest. `Replace` or `Add`, then the hinge is clamped. |
| `deformSkinLine` | A primitive with `"skin": true` follows bone influence radii. |
| `convexParts` | Concave outlines become convex pieces of at most 8 vertices for Box2D. |
| `createRagdoll` | One body per frame, a revolute joint per hinge, limits included. |
| Sampling | Chord-error evaluation into world-space `Polyline2` / `Polygon2` |
| `clipper_ops` | Union, difference, intersection, inflate (Clipper2 2.0) |
| `mesh` | Fill and thick stroke as triangles. Strokes are ribbons, because SFML 3 has no quad primitive. |
| `sfml_draw` | `TriMesh` → `sf::VertexArray` |
| `box2d_export` | Pixels, Y-down → meters, Y-up. Circles, convex polygons (≤ 8 vertices), open chains. |
| `editor/` | SFML editor for that document. |

The kernel headers do not include SFML. `box2d_export.hpp` does include Box2D, because the adapter's job is to speak that API.

## Build

Dependencies, fetched by CMake if you don't pass a path:

- [Box2D 3.1](https://github.com/erincatto/box2d) (`v3.1.1`)
- [Clipper2 2.0.1](https://github.com/AngusJohnson/Clipper2)
- SFML 3.1, for the playground and the editor (`SFML_DIR` if it isn't on the default search path)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/forma_tests
./build/forma_playground
./build/forma_editor examples/level.json
./build/forma_walk
```

`forma_walk` steps `examples/human.json` and `examples/quadruped.json`. The human's `swing` clip plays on the arms. `examples/soft.json` is a skinned outline: pose the chain in the editor and the body follows the bone radii instead of a parent frame.

`attachConvexParts` splits a concave level outline onto one static body, so a notch is not filled by a hull. `createRagdoll` hangs the same hinges on Box2D revolute joints. Forma angles are clockwise and Y-down, so the joint angles are negated.

`forma_editor examples/tentacle.json` opens the chain. `forma_editor examples/human.json` opens the ragdoll. Both are assets, so the origin starts in the center of the view. The panel is Dear ImGui.

Bind mode edits the rest pose, the shapes, and the hinges. Pose mode bends a bone by dragging it; the arc is the angular limit. Save always writes the bind pose. "Use pose as bind" copies the preview back into the rest pose, and the limits stay relative to that new rest.

A shape lives on a frame, so it rotates with that bone and cannot span two bones. A joint is optional: the child frame's pivot is a node in the parent, and the hinge limit is degrees relative to the rest angle. A primitive marked `skin` is sampled in the bind pose and then deformed by the bone radii. The editor draws the selected bone's radius. A curve is resampled before it is deformed, so the outline stays a curve.

In Pose mode, dragging a bone runs IK on that limb. The chain stops where the parent branches, so a hand drag solves the arm and a foot drag solves the leg. The pole is taken from the hinge: a knee limited to one side bends that way. `solveIk` is the same call you make at runtime, once per limb, after the body is placed. It only writes rotations.

## Hierarchy

A node belongs to one frame. Primitives in a frame still share nodes. A child frame's translation is in its parent, so rotating the head carries the eyes, and rotating a lid does not move the eye. `inherit` can drop translation, rotation, or scale. Non-uniform scale on a frame squashes that part in the parent's axes, children included, unless they turn scale inheritance off.

Positive rotation is clockwise on the Y-down screen. The file stores degrees.

Offline checkouts:

```bash
cmake -S . -B build \
  -DFORMA_BOX2D_DIR=/path/to/box2d \
  -DFORMA_CLIPPER2_DIR=/path/to/Clipper2/CPP \
  -DSFML_DIR=/path/to/sfml/lib/cmake/SFML
```

## Docker

The image builds SFML 3.1, then forma, and runs `forma_tests`. Pick the Ubuntu release with `UBUNTU`. 22.04 is the oldest one this file supports.

```bash
docker build --build-arg UBUNTU=22.04 -t forma:22.04 .
docker build --build-arg UBUNTU=24.04 -t forma:24.04 .
docker run --rm forma:24.04
```

The editor is in the image. On a machine with an X server:

```bash
docker run --rm -e DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix forma:24.04 \
  /src/build/forma_editor examples/level.json
```

## Playground

`examples/playground.cpp` is the integration sample.

- A ground polyline is a Box2D chain. Drag its nodes and the chain is rebuilt.
- A centripetal spline shares one node with a polygon. That node oscillates. The polygon is unioned (Clipper2) with a circle every time the node moves, and the union is filled.
- An amber circle and a convex gem are real Box2D bodies. Their transforms are written back onto the nodes they own, and those nodes are what gets drawn.
- Drag a body to move it. Space tosses the ball. R puts both bodies back.

Coordinates in the node pool are pixels, Y down. The exporter divides by `pixelsPerMeter` (32) and flips Y. A single dynamic fixture uses `buildConvexBody`, which replaces a concave outline with its hull. A level outline uses `attachConvexParts`: the same body gets one fixture per convex piece, at most 8 vertices each, and a notch stays empty. Open chains are one-sided, need 4 points, and do not collide on the first and last segment — `buildChainPoints` pads the ends and turns the front face to `ChainFront::PositiveY` so a ground catches bodies under the default gravity.

## What this version leaves out

Ellipses as their own primitive, holes as an authoring primitive (boolean results can contain them), and gradients. An oriented rectangle is a polygon in a rotated frame, not a separate type. Clips live on the document: joint angles relative to rest, looped across the last key back to the first.
