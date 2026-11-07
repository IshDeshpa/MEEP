#pragma once

#include <glm/fwd.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <cstddef>
#include <string>
#include <type_traits>
#include <vector>
#include "extra/cuda_compat.hpp"
#include "extra/cuda_raii.hpp"
#include "extra/enum_lut.hpp"
#include "state/state.hpp"

using Vertex=glm::dvec3; // might change later if normals/other are added. heterogeneous coords
using Tri=glm::uvec3; // indices pointing to vertices

using VertRef=Tri::value_type;
using TriRef=unsigned int; // index pointing to a triangle (use instead of a Tri&, since those refs may be invalidated since tris are in vectors)

// LUTS
enum class Direction : uint8_t{
  X_PLUS=0,
  X_MINUS,
  Y_PLUS,
  Y_MINUS,
  Z_PLUS,
  Z_MINUS,
  COUNT
};

DEFINE_ENUM_LUT(DIRECTION_LUT, Direction, glm::vec3,
  glm::vec3(1, 0, 0),
  glm::vec3(-1, 0, 0),
  glm::vec3(0, 1, 0),
  glm::vec3(0, -1, 0),
  glm::vec3(0, 0, 1),
  glm::vec3(0, 0, -1),
);

struct Color {
  uint8_t r; uint8_t g; uint8_t b; uint8_t a;
};

DEFINE_ENUM_LUT(ELEMENT_COLOR_LUT, Element, Color, 
    {  0,   0,   0,   0}, // AIR
    { 30, 144, 255, 255}, // WATER
    {139,  90,  43, 255}, // EARTH
    {255,  69,   0, 255}, // LAVA
    {100, 100, 110, 255}, // BLOCK
    {255, 215,   0, 255}, // EMITTER
    { 40,  10,  60, 255}, // VOID
);

// Host-side geometry parsed from an OBJ file.
struct HostGeometryData {
  std::vector<Vertex> verts;
  std::vector<Tri> tris;
};

// Parses an OBJ file on the host (CPU only). Returns false if the file cannot be
// opened or read; throws on malformed content.
bool readObj(const std::string &path, HostGeometryData &out);

// Non-owning geometry view; pointers refer to GPU buffers.
struct DeviceGeometryData {
  const Vertex *verts = nullptr;
  const Tri *tris = nullptr;
  std::size_t vertexCount = 0;   // vertices occupy [0, vertexCount)
  std::size_t triangleCount = 0; // triangles occupy [0, triangleCount)
};
static_assert(std::is_trivially_copyable_v<DeviceGeometryData>);
static_assert(std::is_standard_layout_v<DeviceGeometryData>);

// CPU-side geometry (editable) plus its GPU copy, created by transfer().
class Geometry {
public:
  Geometry() = default;
  Geometry(const std::string &path); // loads the file; throws if it cannot be read
  ~Geometry(); // frees the GPU copy

  Geometry(const Geometry &) = delete;
  Geometry &operator=(const Geometry &) = delete;

  // file -> CPU copy. Host only.
  bool load(const std::string &path);
  // CPU copy -> file. Host only.
  bool save(const std::string &path) const;

  // CPU copy edits. Host only.
  VertRef addVertex(Vertex v);
  void addTriangle(Tri indices);
  void addTriangle(Vertex a, Vertex b, Vertex c);

  // Uploads the CPU copy to the GPU, replacing any previous transfer. Returns the
  // GPU pointer to the const DeviceGeometryData that now describes it.
  const DeviceGeometryData *transfer();

  // Host-side copy of the most recent transfer's handle (GPU pointers and counts).
  const DeviceGeometryData *device() const { return &device_gd; }
  const HostGeometryData *host() const { return &host_gd; }

private:
  HostGeometryData host_gd;
  DeviceGeometryData device_gd; // handle values; buffers are owned by this object
  
  const DeviceGeometryData *d_geom = nullptr; // GPU-resident DeviceGeometryData
};

struct DeviceEntityData {
  const DeviceGeometryData *geom; // geometry data for this entity
  bool *triMask; // same size as geom.triangleCount, represents which triangles to show and which to hide
  Color color;
  glm::dmat4x4 transform;
};
static_assert(std::is_trivially_copyable_v<DeviceEntityData>);
static_assert(std::is_standard_layout_v<DeviceEntityData>);

class DeviceEntity : public DeviceEntityData{
public:
  using Data=DeviceEntityData;

  // Zeroed placeholder, for containers that default-construct; the fields must be set before use
  DeviceEntity() : DeviceEntityData{} {}

  MEEP_HOST_DEVICE DeviceEntity(const DeviceGeometryData *dgd, Color c,
                                glm::dmat4 t);

  MEEP_HOST_DEVICE DeviceEntity(const DeviceGeometryData *dgd, Color c,
                                worldcoord coords, glm::dvec3 rot_euler);

  MEEP_HOST_DEVICE const static glm::dmat4 toTransform(glm::dvec3 translation,
                                          glm::dvec3 rot_euler);
};

// Scan byte array (prefix-sum) and return total of input array as well
unsigned scanByteNaiveLaunch(DeviceBuffer<uint8_t> &gpu_in, DeviceBuffer<uint32_t> &gpu_out, unsigned blocks, unsigned threads_per_block);
MEEP_GLOBAL void scanByteNaiveInit(uint8_t *in, uint32_t *out, std::size_t sz);
MEEP_GLOBAL void scanByteNaiveStep(uint32_t *in, uint32_t *out, uint8_t d, std::size_t sz);

