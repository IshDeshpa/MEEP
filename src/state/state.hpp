#pragma once
#include <cstdint>
#include <stdint.h>

// TODO: we could wrap some of these with classes that have APIs to operate on the data,
// these are just the core, minimal reprs. of data to send to the GPU

// A cell contains all the information that needs to be in world state about
// a single cell
struct Cell{
  uint8_t elem: 4;
  uint8_t state: 4; // expand out to separate fields later based on what we want
};

enum Elem{
  // WATER = 0,
  // FIRE = 1,
  // etc.
};

// A single world state buffer
// TODO: like mentioned, above, maybe needs a wrapper class? imagine CPU wants to get a set of adjacent cells (like in a 
// margolus neighborhood), would be nice to have convenient constexpr functions for that
template<int N=0, int M=0, int K=0>
using WorldStateBuf=Cell[N][M][K];

// Two world state buffers, one for current and one for next
template<int N=0, int M=0, int K=0>
struct WorldStateBuf2{
  WorldStateBuf<N,M,K> curr;
  WorldStateBuf<N,M,K> next;
};

// TODO: not positive about this; C standard doesn't have any guarantees
// about ordering of fields in a bitfield struct. Perhaps switch to a simple
// uint16_t with constexpr (compile-time) functions to access the fields?


/*
* e00 e01
* e10 e11
*/
struct ETile{
  union{
    struct {
      // nibbles
      uint8_t e00 : 4;
      uint8_t e01 : 4;
      uint8_t e10 : 4;
      uint8_t e11 : 4;
    };
    struct {
      // bytes
      // TODO: probably check endianness; is this really where we think it is? is it rows or columns?
      uint8_t e0x : 8;
      uint8_t e1x : 8;
    };
    // word
    uint16_t e;
  };
};

// Our "FSM", really a lookup from input ETile -> output ETile
using ETileLUT=ETile[UINT16_MAX+1];

// Sizing assertions, change later if necessary
static_assert(sizeof(Cell) == 1);
static_assert(sizeof(ETile) == sizeof(uint16_t));
