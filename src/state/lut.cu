#include "lut.hpp"
#include "state.hpp"

// Face encoding: material IDs in top-left, top-right, bottom-left, bottom-right order.

__host__ __device__
FaceETile packFace(const CubeTile& tile, unsigned tl, unsigned tr,
                   unsigned bl, unsigned br) {
    return static_cast<FaceETile>(
        (static_cast<unsigned>(tile.cells[tl].elem) << 12) |
        (static_cast<unsigned>(tile.cells[tr].elem) <<  8) |
        (static_cast<unsigned>(tile.cells[bl].elem) <<  4) |
         static_cast<unsigned>(tile.cells[br].elem));
}

__host__ __device__
void unpackFace(FaceETile face, CubeTile& tile, unsigned tl, unsigned tr,
                 unsigned bl, unsigned br) {
    tile.cells[tl].elem = static_cast<Element>((face >> 12) & 0xF);
    tile.cells[tr].elem = static_cast<Element>((face >>  8) & 0xF);
    tile.cells[bl].elem = static_cast<Element>((face >>  4) & 0xF);
    tile.cells[br].elem = static_cast<Element>( face        & 0xF);
}

