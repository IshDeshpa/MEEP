#pragma once

#include "renderGL/cube.hpp"
#include "renderGL/window.hpp"
#include "state/state.hpp"
#include "state/visual.hpp"
#include <vector>

struct RenderMeshGL{
  RenderMeshGL(std::vector<float> verts, std::vector<unsigned> indices);
  GLuint vao = 0;
  GLuint vbo = 0;
  GLuint ebo = 0;
};

struct CellInstance {
  // integer coords
  Coord coord;
  // color
  Color color;
  // which faces to display for this instance in particular
  // bitmask; ordered by Direction enum starting with LSB
  uint8_t facemask;

  // any other data that may need to be associated with a SPECIFIC instance of a cell for rendering
};

class RendererGL{
public:
  bool render(WorldState &w);
  bool isGLContextCurrent() const;
  bool buildVisibleCells(WorldState &ws);
  static RendererGL &instance();

  RendererGL(const RendererGL &) = delete;
  RendererGL &operator=(const RendererGL &) = delete;
  ~RendererGL();

  cudaStream_t render_stream;
  RenderMeshGL cube;

private:
  RendererGL();

  cudaGraphicsResource *instances_cuda;
  GLuint instances_vbo;
  unsigned instances_capacity = 64;
};

