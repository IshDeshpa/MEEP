#pragma once

#include <glm/glm.hpp>
using namespace glm;

#include <state/state.hpp>
#include <stddef.h>
#include <memory>

#include "render/primitives.hpp"

// CUDA functions to expose
void helloWorld(); // to test

constexpr Color ELEMENT_COLOR_LUT[] = {
    {  0,   0,   0,   0}, // AIR
    { 30, 144, 255, 255}, // WATER
    {139,  90,  43, 255}, // EARTH
    {255,  69,   0, 255}, // LAVA
    {100, 100, 110, 255}, // BLOCK
    {255, 215,   0, 255}, // EMITTER
    { 40,  10,  60, 255}, // VOID
};
static_assert(static_cast<int>(Element::COUNT) == std::size(ELEMENT_COLOR_LUT));

struct CellRef{
  Cell &cell;
  coord coords;
};

class CellRenderEntity : public Entity{
  CellRef &cr;

  CellRenderEntity(CellRef &cr, Mesh &m, Color c): Entity(m, c, cr.coords, dvec3(0, 0, 0)), cr(cr){}

  bool updateFaces(WorldState &ws);

  static std::unique_ptr<CellRenderEntity> create(CellRef &c, WorldState &ws); // factory function, call with CellRenderEntity.create

};

