#include <cuda_runtime.h>
#include "lookup_helpers.h"
#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void check(cudaError_t error, const char* operation)
{
    if (error != cudaSuccess)
        throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(error));
}
#define CUDA_CHECK(call) check((call), #call)

// Space 0: global byte table; 1: shared byte table; 2: shared packed words.
// Volatile loads intentionally keep repeated accesses in this microbenchmark.
// CUDA volatile is NOT synchronization. Shared initialization uses a barrier.
template <int Space, int Pattern>
__global__ void lookup_kernel(const std::uint8_t* table, std::uint32_t mask,
                              std::uint32_t iterations, std::uint32_t* output)
{
    __shared__ std::uint32_t words[64];
    if constexpr (Space != 0) {
        for (unsigned w = threadIdx.x; w < 64; w += blockDim.x) {
            const unsigned i = w * 4;
            words[w] = std::uint32_t(table[i])
                | (std::uint32_t(table[i + 1]) << 8)
                | (std::uint32_t(table[i + 2]) << 16)
                | (std::uint32_t(table[i + 3]) << 24);
        }
        __syncthreads(); // All table writes finish before any reads.
        mask = 255u;
    }
    const unsigned thread = blockIdx.x * blockDim.x + threadIdx.x;
    const volatile std::uint8_t* global_bytes = table;
    const volatile std::uint8_t* shared_bytes =
        reinterpret_cast<const volatile std::uint8_t*>(words);
    const volatile std::uint32_t* shared_words = words;
    std::uint32_t sum = 0;
    for (unsigned i = 0; i < iterations; ++i) {
        const unsigned index = lookup_index<Pattern>(thread, i, mask);
        if constexpr (Space == 0) sum += global_bytes[index];
        else if constexpr (Space == 1) sum += shared_bytes[index];
        else sum += unpack_byte(shared_words[index >> 2], index & 3u);
    }
    output[thread] = sum;
}

constexpr const char* spaces[] = {"global_byte", "shared_byte", "shared_packed"};
constexpr const char* patterns[] = {
    "same_byte", "same_word_4bytes", "consecutive_bytes",
    "two_words_same_bank", "pseudo_random"
};
constexpr int threads_per_block = 256;

template <int Space, int Pattern>
void run_case(const std::uint8_t* table, unsigned table_bytes, unsigned blocks,
              unsigned iterations, unsigned repeats, std::uint32_t* output)
{
    const unsigned mask = Space == 0 ? table_bytes - 1 : 255u;
    // Untimed warm-up, including CUDA's first-use overhead.
    lookup_kernel<Space, Pattern><<<blocks, threads_per_block>>>(table, mask, iterations, output);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    cudaEvent_t begin, end;
    CUDA_CHECK(cudaEventCreate(&begin));
    CUDA_CHECK(cudaEventCreate(&end));
    std::vector<float> samples;
    for (unsigned r = 0; r < repeats; ++r) {
        CUDA_CHECK(cudaEventRecord(begin));
        lookup_kernel<Space, Pattern><<<blocks, threads_per_block>>>(table, mask, iterations, output);
        CUDA_CHECK(cudaGetLastError());
        CUDA_CHECK(cudaEventRecord(end));
        CUDA_CHECK(cudaEventSynchronize(end));
        float ms = 0;
        CUDA_CHECK(cudaEventElapsedTime(&ms, begin, end));
        samples.push_back(ms);
    }
    CUDA_CHECK(cudaEventDestroy(begin));
    CUDA_CHECK(cudaEventDestroy(end));

    // Check every lane of the first and last warp against a CPU reference.
    // Copies and CPU reference calculations are outside the timed region.
    for (unsigned first : {0u, blocks * threads_per_block - 32u}) {
        std::uint32_t observed[32];
        CUDA_CHECK(cudaMemcpy(observed, output + first, sizeof observed, cudaMemcpyDeviceToHost));
        for (unsigned lane = 0; lane < 32; ++lane) {
            std::uint32_t expected = 0;
            for (unsigned i = 0; i < iterations; ++i)
                expected += table_value(lookup_index<Pattern>(first + lane, i, mask));
            if (observed[lane] != expected)
                throw std::runtime_error("CPU/GPU checksum mismatch");
        }
    }
    std::sort(samples.begin(), samples.end());
    const float ms = samples.size() % 2 ? samples[samples.size() / 2]
        : (samples[samples.size() / 2 - 1] + samples[samples.size() / 2]) / 2;
    const double lookups = double(blocks) * threads_per_block * iterations;
    // Aggregate throughput, NOT the latency of a single lookup.
    std::cout << spaces[Space] << ',' << patterns[Pattern] << ','
              << (Space == 0 ? table_bytes : 256u) << ',' << ms << ','
              << lookups / (double(ms) * 1e6) << ",PASS\n";
}

template <int Space>
void run_patterns(const std::uint8_t* table, unsigned bytes, unsigned blocks,
                  unsigned iterations, unsigned repeats, std::uint32_t* output, int selected)
{
    if (selected < 0 || selected == 0) run_case<Space, 0>(table, bytes, blocks, iterations, repeats, output);
    if (selected < 0 || selected == 1) run_case<Space, 1>(table, bytes, blocks, iterations, repeats, output);
    if (selected < 0 || selected == 2) run_case<Space, 2>(table, bytes, blocks, iterations, repeats, output);
    if (selected < 0 || selected == 3) run_case<Space, 3>(table, bytes, blocks, iterations, repeats, output);
    if (selected < 0 || selected == 4) run_case<Space, 4>(table, bytes, blocks, iterations, repeats, output);
}

static unsigned positive_argument(const char* text)
{
    if (*text == '-') throw std::invalid_argument("Arguments must be positive integers");
    std::size_t end = 0;
    const auto n = std::stoull(text, &end);
    if (text[end] != '\0' || n == 0 || n > 0xffffffffULL)
        throw std::invalid_argument("Argument is outside uint32 range");
    return static_cast<unsigned>(n);
}

int main(int argc, char** argv)
{
    try {
        if (argc > 7)
            throw std::invalid_argument("Usage: ./lookup_memory [TABLE_BYTES [ITERATIONS [BLOCKS [REPEATS [SPACE [PATTERN]]]]]]");
        const unsigned bytes = argc > 1 ? positive_argument(argv[1]) : 256;
        const unsigned iterations = argc > 2 ? positive_argument(argv[2]) : 10000;
        const unsigned blocks = argc > 3 ? positive_argument(argv[3]) : 128;
        const unsigned repeats = argc > 4 ? positive_argument(argv[4]) : 5;
        const int space = argc > 5 ? std::stoi(argv[5]) : -1;
        const int pattern = argc > 6 ? std::stoi(argv[6]) : -1;
        if (space < -1 || space > 2 || pattern < -1 || pattern > 4)
            throw std::invalid_argument("SPACE must be 0..2; PATTERN must be 0..4 (or -1 for all)");
        if (bytes < 256 || (bytes & (bytes - 1)) != 0)
            throw std::invalid_argument("TABLE_BYTES must be a power of two >= 256");
        if (blocks > 65535)
            throw std::invalid_argument("Use at most 65535 blocks for this test");
        int device = 0, runtime = 0, driver = 0;
        cudaDeviceProp prop{};
        CUDA_CHECK(cudaGetDevice(&device));
        CUDA_CHECK(cudaGetDeviceProperties(&prop, device));
        CUDA_CHECK(cudaRuntimeGetVersion(&runtime));
        CUDA_CHECK(cudaDriverGetVersion(&driver));
        std::cout << "# GPU=" << prop.name << "; CC=" << prop.major << '.' << prop.minor
                  << "; SMs=" << prop.multiProcessorCount << "; L2_bytes=" << prop.l2CacheSize
                  << "; device_memory_bytes=" << prop.totalGlobalMem
                  << "; runtime=" << runtime << "; driver=" << driver << '\n'
                  << "# blocks=" << blocks << "; threads_per_block=" << threads_per_block
                  << "; iterations=" << iterations << "; repeats=" << repeats << '\n'
                  << "space,pattern,table_bytes,median_ms,Glookups_per_second,validation\n";
        std::vector<std::uint8_t> host(bytes);
        for (unsigned i = 0; i < bytes; ++i) host[i] = table_value(i);
        std::uint8_t* table = nullptr;
        std::uint32_t* output = nullptr;
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&table), bytes));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&output),
                              std::size_t(blocks) * threads_per_block * sizeof(*output)));
        CUDA_CHECK(cudaMemcpy(table, host.data(), bytes, cudaMemcpyHostToDevice));
        if (space < 0 || space == 0) run_patterns<0>(table, bytes, blocks, iterations, repeats, output, pattern);
        if (space < 0 || space == 1) run_patterns<1>(table, bytes, blocks, iterations, repeats, output, pattern);
        if (space < 0 || space == 2) run_patterns<2>(table, bytes, blocks, iterations, repeats, output, pattern);
        CUDA_CHECK(cudaFree(output));
        CUDA_CHECK(cudaFree(table));
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
