#pragma once
#include <cstddef>
#include <cstdint>
#include <stdint.h>

// TODO: we could wrap some of these with classes that have APIs to operate on the data,
// these are just the core, minimal reprs. of data to send to the GPU


// TODO: change debug elements to be highest number elements in order to use shifting to quickly differentiate 
enum class Element : std::uint8_t {
  AIR = 0,
  WATER = 1,
  EARTH = 2,
  LAVA = 3,
  // debug
  BLOCK = 4,
  EMITTER = 5,
  VOID = 6
};

// A cell contains all the information that needs to be in world state about
// a single cell
struct Cell{
  Element elem: 4;
  uint8_t state: 4; // expand out to separate fields later based on what we want
};


using dim_t = std::size_t;
struct WorldState {
  std::size_t n, m, k;
  Cell* curr;
  Cell* next;

  WorldState(dim_t n, dim_t m, dim_t k);
  ~WorldState();
  WorldState(const WorldState&) = delete;
  WorldState& operator=(const WorldState&) = delete;
  dim_t index(dim_t x, dim_t y, dim_t z) const;

  //TODO: margolus neighborhood fetch & modify functions. Neighborhoods are either 2x2x2 starting at even (0,0,0) or at odd (1,1,1) coords.
  //      Need these for both CPU & GPU, these will probably be different
};


// TODO: not positive about this; C standard doesn't have any guarantees
// about ordering of fields in a bitfield struct. Perhaps switch to a simple
// uint16_t with constexpr (compile-time) functions to access the fields?


/*
* e00 e01
* e10 e11
*/
using ETile = uint16_t; // 0-15:12, 1-11:8, 2-7:4, 3-3:0

/* 0x0123 = 
* air   water
* earth lava
*/
constexpr ETile cell0 = 0xF000;
constexpr ETile cell1 = 0x0F00;
constexpr ETile cell2 = 0x00F0;
constexpr ETile cell3 = 0x000F;

// Our "FSM", really a lookup from input ETile -> output ETile
using ETileLUT=ETile[UINT16_MAX+1];

// Sizing assertions, change later if necessary
static_assert(sizeof(Cell) == 1);
static_assert(sizeof(ETile) == sizeof(uint16_t));

