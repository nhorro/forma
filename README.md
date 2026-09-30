# forma

A small C++20 geometry kernel for procedural 2D animation. Nodes are shared. Shapes and curves store ids, not copies. Sampling turns them into polylines and polygons, and that baked form is what Clipper2 and Box2D see.

SFML and Box2D are adapters. They are not the model.

## Pieces

| Layer | What it is |
|---|---|
| `NodePool` | Positions with stable `NodeId`s. Moving a node moves every primitive that references it. |
| Paths | `Polyline`, centripetal `CatmullRom` |
| Fills | `Polygon` (screen-space counter-clockwise), `Circle` |
| Sampling | Chord-error evaluation into `Polyline2` / `Polygon2` |
| `clipper_ops` | Union, difference, intersection, inflate (Clipper2 2.0) |
| `mesh` | Fill and thick stroke as triangles. Strokes are ribbons, because SFML 3 has no quad primitive. |
| `sfml_draw` | `TriMesh` → `sf::VertexArray` |
| `box2d_export` | Pixels, Y-down → meters, Y-up. Circles, convex polygons (≤ 8 vertices), open chains. |

The kernel headers do not include SFML. `box2d_export.hpp` does include Box2D, because the adapter's job is to speak that API.

## Build

Dependencies, fetched by CMake if you don't pass a path:

- [Box2D 3.1](https://github.com/erincatto/box2d) (`v3.1.1`)
- [Clipper2 2.0.1](https://github.com/AngusJohnson/Clipper2)
- SFML 3.1, only for the playground (`SFML_DIR` if it isn't on the default search path)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/forma_tests
./build/forma_playground
```

Offline checkouts:

```bash
cmake -S . -B build \
  -DFORMA_BOX2D_DIR=/path/to/box2d \
  -DFORMA_CLIPPER2_DIR=/path/to/Clipper2/CPP \
  -DSFML_DIR=/path/to/sfml/lib/cmake/SFML
```

## Playground

`examples/playground.cpp` is the integration sample.

- A ground polyline is a Box2D chain. Drag its nodes and the chain is rebuilt.
- A centripetal spline shares one node with a polygon. That node oscillates. The polygon is unioned (Clipper2) with a circle every time the node moves, and the union is filled.
- An amber circle and a convex gem are real Box2D bodies. Their transforms are written back onto the nodes they own, and those nodes are what gets drawn.
- Drag a body to move it. Space tosses the ball. R puts both bodies back.

Coordinates in the node pool are pixels, Y down. The exporter divides by `pixelsPerMeter` (32) and flips Y. Box2D polygons have to be convex and have at most 8 vertices; concave input is replaced by its hull and reported as `simplified`. Open chains are one-sided, need 4 points, and do not collide on the first and last segment — `buildChainPoints` pads the ends and turns the front face to `ChainFront::PositiveY` so a ground catches bodies under the default gravity.

## What this version leaves out

Ellipses, oriented rectangles, holes as an authoring primitive (boolean results can contain them), gradients, and convex decomposition of concave fixtures. Add those at the sampling boundary rather than inside Box2D types.
