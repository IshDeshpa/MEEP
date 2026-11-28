#pragma once

#include "extra/cuda_compat.hpp"
#include "lut.hpp"

#include <cuda_runtime.h>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <glm/vec3.hpp>

using dim = std::size_t;
using Coord = glm::ivec3;

enum class Element : std::uint8_t {
  AIR = 0, WATER = 1, EARTH = 2, LAVA = 3,
  NATURE = 4, STEAM = 5, FIRE = 6, ACID = 7,
  ICE = 8, HOTAIR = 9, EXPLOSION = 10, SAND = 11,
  BOMB = 12, BLOCK = 13, EMITTER = 14, VOID = 15
};
inline constexpr std::size_t ELEMENT_COUNT = 16;

struct Cell {
  Element elem: 4;
  std::uint8_t state: 4;
};

struct CubeTile { Cell cells[8]; };
static_assert(sizeof(Cell) == 1);
static_assert(sizeof(CubeTile) == 8 * sizeof(Cell));

struct DeviceWorldView {
  const Cell* curr;
  Cell* next;
  std::size_t n_pad, m_pad, k_pad;

  MEEP_HOST_DEVICE std::size_t visibleN() const noexcept { return n_pad - 2; }
  MEEP_HOST_DEVICE std::size_t visibleM() const noexcept { return m_pad - 2; }
  MEEP_HOST_DEVICE std::size_t visibleK() const noexcept { return k_pad - 2; }
  MEEP_HOST_DEVICE std::size_t size() const noexcept {
    return visibleN() * visibleM() * visibleK();
  }

  MEEP_HOST_DEVICE
  std::size_t index(std::size_t x, std::size_t y, std::size_t z) const noexcept;
  MEEP_HOST_DEVICE
  Coord coord(std::size_t ind) const noexcept {
    const std::size_t visible_m = visibleM(), visible_k = visibleK();
    return Coord(static_cast<int>(ind / (visible_m * visible_k)),
                      static_cast<int>((ind / visible_k) % visible_m),
                      static_cast<int>(ind % visible_k));
  }
  MEEP_HOST_DEVICE Cell readCell(std::size_t x, std::size_t y,
                                 std::size_t z) const noexcept;
  MEEP_HOST_DEVICE Cell readCell(Coord c) const noexcept;
  MEEP_HOST_DEVICE CubeTile readTile(std::size_t x, std::size_t y,
                                     std::size_t z) const noexcept;
  MEEP_HOST_DEVICE CubeTile readTile(Coord c) const noexcept;
  MEEP_HOST_DEVICE void writeCell(std::size_t x, std::size_t y,
                                  std::size_t z, Cell cell) const noexcept;
  MEEP_HOST_DEVICE void writeCell(Coord c, Cell cell) const noexcept;
  MEEP_HOST_DEVICE void writeTile(std::size_t x, std::size_t y,
                                  std::size_t z, const CubeTile& tile) const noexcept;
  MEEP_HOST_DEVICE void writeTile(Coord c, const CubeTile& tile) const noexcept;
};
static_assert(std::is_trivially_copyable_v<DeviceWorldView>);

struct WorldState {
  dim n, m, k;
  std::size_t n_pad, m_pad, k_pad;
  Cell* world;
  Cell* d_curr;
  Cell* d_next;
  // Per-world view, refreshed whenever the GPU buffers change roles.
  DeviceWorldView device_view;
  cudaStream_t world_tick_stream;

  static std::tuple<size_t, size_t, size_t> validateWorldFile(const char* f);
  WorldState(std::tuple<size_t, size_t, size_t> dims);
  WorldState(size_t n, size_t m, size_t k);
  WorldState(const char* f);
  ~WorldState();
  WorldState(const WorldState&) = delete;
  WorldState& operator=(const WorldState&) = delete;
  size_t index(size_t x, size_t y, size_t z) const;
  size_t index(Coord c) const;
  Cell& operator()(Coord c);
  const Cell& operator()(Coord c) const;
  size_t unPaddedIndex(size_t x, size_t y, size_t z) const;
  size_t paddedIndex(size_t x, size_t y, size_t z) const;
  DeviceWorldView deviceView() const;
  void updateWorld(std::size_t tick, LUT& lut);
  void waitForTick();
  void swapBuffers();
};
