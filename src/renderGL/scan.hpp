#pragma once

#include "extra/device_resources.hpp"
#include <cstdint>

// Exclusive scan of nonzero bytes; returns the number of nonzero inputs.
unsigned scanByteNaiveLaunch(DeviceBuffer<uint8_t> &gpu_in,
                             DeviceBuffer<uint32_t> &gpu_out,
                             unsigned blocks, unsigned threads_per_block);
