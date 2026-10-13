#ifndef LOOKUP_HELPERS_H
#define LOOKUP_HELPERS_H
#include <cstdint>
#ifdef __CUDACC__
#define LOOKUP_HD __host__ __device__
#else
#define LOOKUP_HD
#endif

// Shared by the CUDA kernel and CPU reference.
template <int Pattern>
LOOKUP_HD inline std::uint32_t lookup_index(std::uint32_t thread,
                                           std::uint32_t iteration,
                                           std::uint32_t mask)
{
    const std::uint32_t lane = thread & 31u;
    const std::uint32_t warp = thread >> 5;
    const std::uint32_t base = warp * 128u + iteration * 32u;
    if constexpr (Pattern == 0) return base & mask; // same byte per warp
    if constexpr (Pattern == 1) return (base + (lane & 3u)) & mask;
    if constexpr (Pattern == 2) return (base + lane) & mask;
    if constexpr (Pattern == 3)
        return (base + (lane & 1u) * 128u + (iteration & 3u)) & mask;
    // Deterministic pseudo-random indices, not a statistical RNG benchmark.
    std::uint32_t x = thread * 747796405u + iteration * 2891336453u;
    x ^= x >> 16;
    x *= 2246822519u;
    x ^= x >> 13;
    return x & mask;
}

LOOKUP_HD inline std::uint8_t table_value(std::uint32_t index)
{
    return static_cast<std::uint8_t>((index * 37u + 13u) ^ (index >> 8));
}
LOOKUP_HD inline std::uint32_t pack_four(std::uint32_t first)
{
    return std::uint32_t(table_value(first))
        | (std::uint32_t(table_value(first + 1)) << 8)
        | (std::uint32_t(table_value(first + 2)) << 16)
        | (std::uint32_t(table_value(first + 3)) << 24);
}
LOOKUP_HD inline std::uint8_t unpack_byte(std::uint32_t word, unsigned position)
{
    return static_cast<std::uint8_t>((word >> (position * 8u)) & 255u);
}
#undef LOOKUP_HD
#endif
