// Standalone CPU -> GPU upload test. No project headers or kernels required.
// WSL: /usr/local/cuda-12.6/bin/nvcc -O3 -std=c++17 transfer_bw.cu -o transfer_bw
// Run: ./transfer_bw 128 20       (MiB per upload, timed uploads per mode)
#include <cuda_runtime.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

void check(cudaError_t result, const char* call) {
    if (result != cudaSuccess)
        throw std::runtime_error(std::string(call) + ": " + cudaGetErrorString(result));
}
#define CUDA_CHECK(call) check((call), #call)

constexpr std::size_t MiB = 1024 * 1024;

struct DeviceBuffer {
    std::uint8_t* data = nullptr;
    explicit DeviceBuffer(std::size_t bytes) {
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&data), bytes));
    }
    ~DeviceBuffer() { if (data) cudaFree(data); }
    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;
};

struct HostBuffer {
    std::unique_ptr<std::uint8_t[]> ordinary;
    std::uint8_t* data = nullptr;
    bool pinned;
    HostBuffer(std::size_t bytes, bool use_pinned) : pinned(use_pinned) {
        if (pinned)
            CUDA_CHECK(cudaMallocHost(reinterpret_cast<void**>(&data), bytes));
        else {
            ordinary.reset(new std::uint8_t[bytes]);
            data = ordinary.get();
        }
    }
    ~HostBuffer() { if (pinned && data) cudaFreeHost(data); }
    HostBuffer(const HostBuffer&) = delete;
    HostBuffer& operator=(const HostBuffer&) = delete;
};

// One explicit byte per cell: low nibble = element, high nibble = state.
// Mix the index so the buffer is varied rather than all air or a single byte.
std::uint8_t fake_cell(std::size_t index) {
    std::uint32_t v = static_cast<std::uint32_t>(index);
    v ^= v >> 16; v *= 0x7feb352du;
    v ^= v >> 15; v *= 0x846ca68bu;
    v ^= v >> 16;
    const unsigned elem = v & 15u;
    const unsigned state = (v >> 8) & 15u;
    return static_cast<std::uint8_t>(elem | (state << 4));
}

void benchmark(std::uint8_t* gpu, std::size_t bytes, unsigned repeats, bool pinned) {
    HostBuffer host(bytes, pinned);
    for (std::size_t i = 0; i < bytes; ++i) host.data[i] = fake_cell(i);

    // Allocation, initialization, and warmup are outside the measured region.
    for (unsigned i = 0; i < 3; ++i) {
        CUDA_CHECK(cudaMemcpy(gpu, host.data, bytes, cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaDeviceSynchronize());
    }

    using Clock = std::chrono::steady_clock;
    std::vector<double> times;
    times.reserve(repeats);
    for (unsigned i = 0; i < repeats; ++i) {
        const auto begin = Clock::now();
        CUDA_CHECK(cudaMemcpy(gpu, host.data, bytes, cudaMemcpyHostToDevice));
        // Explicit completion matters, especially with pageable host memory.
        CUDA_CHECK(cudaDeviceSynchronize());
        const auto end = Clock::now();
        times.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
    }

    // Verify every byte by reading back in small chunks, outside upload timing.
    std::vector<std::uint8_t> readback(std::min(bytes, MiB));
    for (std::size_t offset = 0; offset < bytes; offset += readback.size()) {
        const auto count = std::min(readback.size(), bytes - offset);
        CUDA_CHECK(cudaMemcpy(readback.data(), gpu + offset, count, cudaMemcpyDeviceToHost));
        if (!std::equal(readback.begin(), readback.begin() + count, host.data + offset))
            throw std::runtime_error("Upload verification failed");
    }

    std::sort(times.begin(), times.end());
    const std::size_t mid = times.size() / 2;
    const double median = times.size() % 2 ? times[mid] : (times[mid - 1] + times[mid]) / 2;
    const double gbps = static_cast<double>(bytes) / (median * 1e6);
    std::cout << std::left << std::setw(20) << (pinned ? "Pinned CPU memory" : "Ordinary CPU memory")
              << std::right << std::fixed << std::setprecision(3)
              << std::setw(14) << median << std::setw(14) << gbps << "      PASS\n";
}

unsigned long long positive_number(const char* arg) {
    const std::string text(arg);
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("Arguments must be positive whole numbers");
    const auto value = std::stoull(text);
    if (value == 0) throw std::invalid_argument("Arguments must be positive");
    return value;
}

int main(int argc, char** argv) {
    try {
        if (argc > 3) throw std::invalid_argument("Usage: ./transfer_bw [MiB=128] [repeats=20]");
        const auto mib = argc > 1 ? positive_number(argv[1]) : 128ull;
        const auto reps = argc > 2 ? positive_number(argv[2]) : 20ull;
        if (mib > std::numeric_limits<std::size_t>::max() / MiB || reps > 100000)
            throw std::invalid_argument("Requested size or repeat count is too large");
        const std::size_t bytes = static_cast<std::size_t>(mib) * MiB;
        CUDA_CHECK(cudaSetDevice(0));
        cudaDeviceProp props{};
        CUDA_CHECK(cudaGetDeviceProperties(&props, 0));
        DeviceBuffer gpu(bytes);

        std::cout << "GPU: " << props.name << '\n'
                  << "Upload: " << mib << " MiB (" << bytes << " one-byte cells)\n"
                  << "Timed uploads per mode: " << reps << " (plus 3 warmups)\n"
                  << "Synthetic IDs: 0..15; state: 0..15\n\n"
                  << std::left << std::setw(20) << "CPU allocation"
                  << std::right << std::setw(14) << "Median ms"
                  << std::setw(14) << "Upload GB/s" << "      Check\n";
        benchmark(gpu.data, bytes, static_cast<unsigned>(reps), false);
        benchmark(gpu.data, bytes, static_cast<unsigned>(reps), true);
        std::cout << "\nGB/s = bytes / seconds / 1,000,000,000.\n"
                  << "Wall time includes cudaMemcpy and waiting for completion.\n"
                  << "Allocation, data generation, warmup, and verification are excluded.\n"
                  << "This measures repeated CPU -> GPU uploads, not GPU DRAM bandwidth.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
