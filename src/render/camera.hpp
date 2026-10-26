#pragma once

#define GLM_FORCE_CUDA
#include <glm/glm.hpp>

#include "render/render.hpp"

class Screen {
public:

private:
  const unsigned WIDTH; // pixels
  const unsigned HEIGHT; // pixels
  std::vector<Color> framebuffer;
  std::vector<double> depthbuffer;
};

class Camera {
public:

private:
  // Will change as we move
  Pose pose;

  // Fixed at construction (think like camera's inherent parameters)
  const double Z_NEAR;
  const double Z_FAR;
  const double FOV_DEG;
  Screen screen;
};
