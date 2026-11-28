#pragma once

#include "state/state.hpp"
#include "extra/enum_lut.hpp"

enum class Direction : std::uint8_t {
  X_PLUS = 0, X_MINUS, Y_PLUS, Y_MINUS, Z_PLUS, Z_MINUS, COUNT
};

struct Color {
  uint8_t r, g, b, a;
};

DEFINE_ENUM_LUT(DIRECTION_LUT, Direction, glm::vec3,
  glm::vec3(1, 0, 0),
  glm::vec3(-1, 0, 0),
  glm::vec3(0, 1, 0),
  glm::vec3(0, -1, 0),
  glm::vec3(0, 0, 1),
  glm::vec3(0, 0, -1),
);

DEFINE_ENUM_LUT_SIZED(ELEMENT_COLOR_LUT, Element, Color, ELEMENT_COUNT,
    {  0,   0,   0,   0}, // AIR
    { 30, 140, 255,  76}, // WATER
    {110,  70,  50, 255}, // EARTH
    {255,  69,   0, 255}, // LAVA
    { 35, 175,  60, 255}, // NATURE
    {200, 200, 200,  64}, // STEAM
    {255, 120,   0,  64}, // FIRE
    {175, 255,   0, 230}, // ACID
    {110, 240, 255, 128}, // ICE
    {255, 165, 165,  76}, // HOTAIR
    {255, 230, 150, 240}, // EXPLOSION
    {220, 200, 150, 255}, // SAND
    {115, 110, 100, 255}, // BOMB
    {100, 100, 110, 255}, // BLOCK
    {210, 210, 200, 255}, // EMITTER
    { 35,  10,  60, 255}, // VOID
);

