#pragma once

#include "check.hpp"
#include <cuda_runtime.h>
#include <cstddef>
#include <stdexcept>
#include <utility>

template<typename T>
class DeviceBuffer{
  T *ptr = nullptr;
  std::size_t sz;

public:
  DeviceBuffer(std::size_t sz): sz(sz){
    CUDA_CHECK(cudaMalloc(&ptr, sz * sizeof(T)));
  }

  // Allocates a new buffer and copies the contents on the device.
  DeviceBuffer(const DeviceBuffer &buf): sz(buf.sz){
    CUDA_CHECK(cudaMalloc(&ptr, bytes()));
    CUDA_CHECK(cudaMemcpy(ptr, buf.ptr, bytes(), cudaMemcpyDeviceToDevice));
  }

  // Copies the contents into this buffer's existing storage; the pointer does not change.
  DeviceBuffer &operator=(const DeviceBuffer &buf){
    if(this == &buf) return *this;
    if(sz != buf.sz) throw std::logic_error("DeviceBuffer: copy-assign requires equal sizes");
    CUDA_CHECK(cudaMemcpy(ptr, buf.ptr, bytes(), cudaMemcpyDeviceToDevice));
    return *this;
  }

  // Moves transfer ownership and leave the source empty (nullptr, size 0).
  DeviceBuffer(DeviceBuffer &&buf) noexcept
    : ptr(std::exchange(buf.ptr, nullptr)), sz(std::exchange(buf.sz, 0)) {}

  // Only valid into an empty buffer. Overwriting live memory is a usage error;
  // use std::swap to exchange two buffers.
  DeviceBuffer &operator=(DeviceBuffer &&buf) {
    if (this == &buf) return *this;
    if (ptr) throw std::logic_error("DeviceBuffer: move-assign into a non-empty buffer; use std::swap");
    ptr = std::exchange(buf.ptr, nullptr);
    sz = std::exchange(buf.sz, 0);
    return *this;
  }

  ~DeviceBuffer(){
    cudaFree(ptr);
  }

  // Raw device pointer
  T *data() const { return ptr; }

  std::size_t size() const { return sz; }
  std::size_t bytes() const { return sz * sizeof(T); }

  // Copies the element at index from device memory to the host
  T get(std::size_t index) const {
    if (index >= sz) throw std::out_of_range("DeviceBuffer::get: index out of range");
    T value;
    CUDA_CHECK(cudaMemcpy(&value, ptr + index, sizeof(T), cudaMemcpyDeviceToHost));
    return value;
  }

  // Copies value from the host into the element at index on the device.
  void set(std::size_t index, const T &value){
    if (index >= sz) throw std::out_of_range("DeviceBuffer::set: index out of range");
    CUDA_CHECK(cudaMemcpy(ptr + index, &value, sizeof(T), cudaMemcpyHostToDevice));
  }

};
