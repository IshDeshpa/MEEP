#pragma once
#include "extra/cuda_compat.hpp"
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <glm/vec3.hpp>

using dim = std::size_t;
using coord = glm::ivec3;

// TODO: reserve the highest material IDs for debug elements.
enum class Element : std::uint8_t {
  AIR = 0,
  WATER = 1,
  EARTH = 2,
  LAVA = 3,
  // debug
  BLOCK = 4,
  EMITTER = 5,
  VOID = 6,
  COUNT = 7
};

// A cell contains all the information that needs to be in world state about
// a single cell
struct Cell {
  Element elem: 4;
  std::uint8_t state: 4; // expand out to separate fields later based on what we want
};

struct DeviceWorldView;

struct WorldState {
  dim n, m, k; // Requested, visible dimensions.
  std::size_t n_pad, m_pad, k_pad; // One BLOCK cell on each side.
  Cell* world; // Padded initialization image on the CPU; not a live GPU mirror.
  Cell* d_curr;
  Cell* d_next;

  WorldState(size_t n, size_t m, size_t k);
  ~WorldState();
  WorldState(const WorldState&) = delete;
  WorldState& operator=(const WorldState&) = delete;
  // Visible coordinates [0,n) x [0,m) x [0,k), shifted into padded storage.
  size_t index(size_t x, size_t y, size_t z) const;
  // Signed renderer coordinates can address the one-cell BLOCK halo.
  size_t index(coord c) const;
  // These access the CPU initialization/snapshot image, not live GPU memory.
  Cell& operator()(coord c);
  const Cell& operator()(coord c) const;
  // Raw padded coordinates; z is contiguous, with strides m_pad*k_pad,
  // k_pad, 1. Rendering should iterate visible dimensions and use index().
  size_t paddedIndex(size_t x, size_t y, size_t z) const;
  // Capture the current device-buffer roles and padded strides for a launch.
  DeviceWorldView deviceView() const;
  void updateWorld(std::size_t tick);
  void waitForTick(); // Wait for the current simulation stream to finish.
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

static_assert(sizeof(Cell) == 1);
static_assert(sizeof(CubeTile) == 8 * sizeof(Cell));

// Non-owning view passed by value; it does not copy world cells.
struct DeviceWorldView {
  const Cell* curr;
  Cell* next;
  std::size_t m_pad, k_pad;

  // Visible (0,0,0) is padded (1,1,1); signed coordinates include the halo.
  MEEP_HOST_DEVICE
  std::size_t index(std::ptrdiff_t x, std::ptrdiff_t y,
                    std::ptrdiff_t z) const noexcept;

  // Caller guarantees the full tile fits, including origins at -1.
  MEEP_HOST_DEVICE
  CubeTile readTile(std::ptrdiff_t x, std::ptrdiff_t y,
                    std::ptrdiff_t z) const noexcept;

  MEEP_HOST_DEVICE
  void writeTile(std::ptrdiff_t x, std::ptrdiff_t y, std::ptrdiff_t z,
                  const CubeTile& tile) const noexcept;
};
static_assert(std::is_trivially_copyable_v<DeviceWorldView>);

