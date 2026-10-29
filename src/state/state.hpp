#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdint.h>

#include <glm/glm.hpp>
using namespace glm;

using dim = std::size_t;
using coord = ivec3;

// TODO: we could wrap some of these with classes that have APIs to operate on the data,
// these are just the core, minimal reprs. of data to send to the GPU


// TODO: change debug elements to be highest number elements in order to use shifting to quickly differentiate 
enum class Element : std::uint8_t {
  AIR = 0,
  WATER,
  EARTH,
  LAVA,
  // debug
  BLOCK,
  EMITTER,
  VOID,
  COUNT
};

// A cell contains all the information that needs to be in world state about
// a single cell
struct Cell{
  Element elem: 4;
  uint8_t state: 4; // expand out to separate fields later based on what we want
};

struct WorldState {
  dim n, m, k;
  Cell* world;
  Cell* d_curr;
  Cell* d_next;

  WorldState(size_t n, size_t m, size_t k);
  ~WorldState();
  WorldState(const WorldState&) = delete;
  WorldState& operator=(const WorldState&) = delete;
  // Flat [x][y][z] storage: z is contiguous; strides are m*k, k, 1.
  size_t index(size_t x, size_t y, size_t z) const;
  size_t index(coord c) const {
    return index(static_cast<size_t>(c.x), static_cast<size_t>(c.y),
                 static_cast<size_t>(c.z));
  }
  Cell& operator()(coord c) const { return world[index(c)]; }
  void updateWorld(std::size_t tick);

  //TODO: margolus neighborhood fetch & modify functions. Neighborhoods are either 2x2x2 starting at even (0,0,0) or at odd (1,1,1) coords.
  //      Need these for both CPU & GPU, these will probably be different
};

/*
 * A 2x2x2 tile containing eight 1-byte Cells.
 * Labels mark cell positions: cell_id = 4*x + 2*y + z.
 * z changes fastest, then y, then x.
 *
 *           6---------7
 *          /|        /|
 *         2---------3 |
 *         | 4-------|-5
 *         |/        |/
 *         0---------1
 *
 *         y: up   x: back (/)   z: right
 *
 * Faces in top-left, top-right, bottom-left, bottom-right order:
 *   x = 0: [2, 3, 0, 1]    x = 1: [6, 7, 4, 5]
 *   z = 1: [3, 7, 1, 5]    z = 0: [2, 6, 0, 4]
 */
struct CubeTile {
  Cell cells[8];
};


/*
* e00 e01
* e10 e11
*/
using FaceETile = uint16_t; // 0-[15:12], 1-[11:8], 2-[7:4], 3-[3:0]

/* 0x0123 = 
* air   water
* earth lava
*/
constexpr FaceETile cell0 = 0xF000;
constexpr FaceETile cell1 = 0x0F00;
constexpr FaceETile cell2 = 0x00F0;
constexpr FaceETile cell3 = 0x000F;

// Our "FSM", really a lookup from input FaceETile -> output FaceETile
using FaceETileLUT=FaceETile[UINT16_MAX+1];

// Sizing assertions, change later if necessary
static_assert(sizeof(Cell) == 1);
static_assert(sizeof(CubeTile) == 8 * sizeof(Cell));
static_assert(sizeof(FaceETile) == sizeof(uint16_t));

