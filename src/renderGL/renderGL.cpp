#include "renderGL/renderGL.hpp"
#include "renderGL/window.hpp"
#include "extra/check.hpp"
#include <cuda_gl_interop.h>
#include <stdexcept>

bool RendererGL::isGLContextCurrent() const {
  return GameWindow::instance().isGLContextCurrent();
}

bool RendererGL::render(WorldState &w){
  
  return true;
}

RendererGL &RendererGL::instance(){
  static RendererGL global_renderer;
  return global_renderer;
}

RendererGL::RendererGL() : cube(Cube::vertices, Cube::indices){
  this->instances_cuda = nullptr;

  glGenBuffers(1, &this->instances_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, this->instances_vbo);

  glBufferData(
    GL_ARRAY_BUFFER,
    instances_capacity * sizeof(CellInstance),
    nullptr,
    GL_DYNAMIC_DRAW
  );

  CUDA_CHECK(cudaGraphicsGLRegisterBuffer(&this->instances_cuda, this->instances_vbo, cudaGraphicsRegisterFlagsNone));
}

RenderMeshGL::RenderMeshGL(std::vector<float> verts, std::vector<unsigned> indices){
  RendererGL &r = RendererGL::instance();

  if(!r.isGLContextCurrent()){
    throw std::runtime_error("Trying to construct a RenderEntityGL while not on the render thread!");
  }

  glGenVertexArrays(1, &vao); // vertex attribute data (for each vertex, what position does what attribute sit at)
  glGenBuffers(1, &vbo); // vertex buffer
  glGenBuffers(1, &ebo); // index buffer
  
  // make this the current target vert attr array
  // all subsequent ops that use vertex attr arrays will assume we use this one
  glBindVertexArray(vao); 

  // GL_ARRAY_BUFFER targets this vbo whenever this vao is used
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(
    GL_ARRAY_BUFFER,
    verts.size() * sizeof(float),
    verts.data(),
    GL_STATIC_DRAW
  );

  // set up the first attribute of a vertex
  // TODO: maybe take in some attribute description and auto-layout this bit?
  glVertexAttribPointer(
    0, // attr loc
    3, // num components (x, y, z)
    GL_FLOAT,
    GL_FALSE, // no normalize
    3 * sizeof(float), // stride to next vert's attribute
    nullptr // pos attribute starts at offset 0 in the VBO
  );
  glEnableVertexAttribArray(0);

  // GL_ELEMENT_ARRAY_BUFFER targets this ebo whenever this vao is used
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(
    GL_ELEMENT_ARRAY_BUFFER,
    indices.size() * sizeof(unsigned),
    indices.data(),
    GL_STATIC_DRAW
  );

  // unbind this vao
  glBindVertexArray(0);
}
