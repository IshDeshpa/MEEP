# MEEP class diagram

```mermaid
classDiagram
direction TB

%% ---------- state ----------
class Cell {
  <<struct>>
  Element elem : 4
  uint8 state : 4
}

class CubeTile {
  <<struct>>
  Cell cells[8]
}

class WorldState {
  <<struct>>
  dim n, m, k
  size_t n_pad, m_pad, k_pad
  Cell* world
  Cell* d_curr
  Cell* d_next
  +index(x, y, z) size_t
  +index(worldcoord) size_t
  +operator()(worldcoord) Cell&
  +paddedIndex(x, y, z) size_t
  +updateWorld(tick, LUT&)
  +waitForTick()
  +swapBuffers()
  +$device_view DeviceWorldView
}

class DeviceWorldView {
  <<struct>>
  const Cell* curr
  Cell* next
  size_t n_pad, m_pad, k_pad
  +index(x, y, z) size_t
  +readCell(x, y, z) Cell
  +readTile(x, y, z) CubeTile
  +writeTile(x, y, z, tile)
}

class LUT {
  <<struct>>
  FaceETile* d_lut
  +deviceView() DeviceLUTView
}

class DeviceLUTView {
  <<struct>>
  const FaceETile* lut
  +lookup(index) FaceETile
}

%% ---------- primitives ----------
class Color {
  <<struct>>
  uint8 r, g, b, a
}

class Pose {
  <<struct>>
  dvec4 pos
  dvec4 look
}

class DeviceGeometryData {
  <<struct>>
  const Vertex* verts
  const Tri* tris
  size_t vertexCount
  size_t triangleCount
}

class DeviceGeometry {
  +DeviceGeometry(string& path)
  +~DeviceGeometry()
  +load(string& path) bool
  +save(string& path) bool
}

class DeviceEntityData {
  <<struct>>
  const DeviceGeometryData& geom
  bool* triMask
  Color color
  dmat4 transform
}

class DeviceEntity {
  +DeviceEntity(dgd, Color, dmat4)
  +DeviceEntity(dgd, Color, worldcoord, dvec3 rot)
  +toTransform(translation, rot) dmat4
}

%% ---------- render ----------
class CellRef {
  <<struct>>
  Cell& cell
  worldcoord coords
}

class CubeFace {
  <<struct>>
  TriRef a, b
  Direction d
}

class HostCube {
  +CUBE_GEOM
  +CUBE_FACES
  +$instance() HostCube&
  -HostCube()
}

class DeviceCubeData {
  <<struct>>
  const CubeFace* faces
}

class DeviceCube {
  +$INSTANCE DeviceCube
  -DeviceCube()
}

class CellRenderEntity {
  CellRef& cell_ref
  +$create(CellRef&, DeviceWorldView) CellRenderEntity*
  -CellRenderEntity(CellRef&)
}

class Screen {
  const unsigned WIDTH, HEIGHT
  vector~Color~ framebuffer
  vector~double~ depthbuffer
}

class Camera {
  Pose pose
  const double Z_NEAR, Z_FAR, FOV_DEG
  Screen screen
}

class Renderer {
  -Renderer()
}

%% ---------- inheritance ----------
DeviceGeometryData <|-- DeviceGeometry
DeviceGeometryData <|-- DeviceCubeData
DeviceCubeData <|-- DeviceCube
DeviceEntityData <|-- DeviceEntity
DeviceEntity <|-- CellRenderEntity

%% ---------- composition / association ----------
WorldState *-- Cell : world / d_curr / d_next
WorldState ..> DeviceWorldView : device_view
DeviceWorldView ..> Cell : reads / writes
DeviceWorldView ..> CubeTile : tiles
CubeTile *-- Cell : cells[8]
LUT ..> DeviceLUTView : deviceView()
DeviceEntityData o-- DeviceGeometryData : geom
DeviceEntityData *-- Color
CellRef o-- Cell : cell
CellRenderEntity o-- CellRef : cell_ref
CellRenderEntity ..> DeviceWorldView : create()
CubeFace ..> Direction
DeviceCube ..> CubeFace : faces
Camera *-- Pose
Camera *-- Screen
Screen *-- Color
Renderer ..> Camera
```

## Notes

- `HostCube` refers to `Geometry`, which is commented out in `render/primitives.cpp`, so it won't compile until that is resolved.
- `Renderer` is an empty stub; its `Camera` dependency is intended, not yet implemented.
- Edges are approximate: `DeviceEntityData` holds `geom` by reference (aggregation), `Color` by value (composition).
