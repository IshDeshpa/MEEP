#include "lut.hpp"
#include "state.hpp"
#include "extra/check.hpp"

#include <cuda_runtime.h>
#include <iostream>
#include <fstream>
#include <filesystem>

// Face encoding: material IDs in top-left, top-right, bottom-left, bottom-right order.
LUT::LUT(const char* f) {
    std::filesystem::path path(f);
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("LUT file does not exist: " + path.string());
    } else if (std::filesystem::file_size(path) != FACE_LUT_SIZE) {
        throw std::runtime_error("LUT file has incorrect size: " + path.string());
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open LUT file: " + path.string());
    }

    CUDA_CHECK(cudaMalloc(&d_lut, FACE_LUT_SIZE));
    FaceETile* h_lut = new FaceETile[FACE_LUT_ELEMS];
    file.read(reinterpret_cast<char*>(h_lut), FACE_LUT_SIZE);
    CUDA_CHECK(cudaMemcpy(d_lut, h_lut, FACE_LUT_SIZE, cudaMemcpyHostToDevice));
    delete[] h_lut;
};
LUT::~LUT() {
    cudaFree(d_lut);
}

DeviceLUTView LUT::deviceView() const {
    return {d_lut};
}

MEEP_HOST_DEVICE
FaceETile DeviceLUTView::lookup(FaceETile index) {
    return lut[index];
}

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

