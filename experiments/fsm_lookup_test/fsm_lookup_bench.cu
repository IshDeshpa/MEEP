#include <cuda_runtime.h>
#include "fsm_helpers.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void check(cudaError_t e, const char* name)
{
    if (e != cudaSuccess) throw std::runtime_error(std::string(name) + ": " + cudaGetErrorString(e));
}
#define CUDA_CHECK(call) check((call), #call)
constexpr unsigned block_threads = 256;

// Human-readable output by default; set FSM_CSV=1 for the original CSV format.
static bool csv_output()
{
    const char* value = std::getenv("FSM_CSV");
    return value && std::string(value) == "1";
}

static std::string memory_size(std::uint64_t bytes)
{
    std::ostringstream text;
    text << std::fixed << std::setprecision(2);
    if (bytes >= (1ULL << 30)) text << double(bytes) / double(1ULL << 30) << " GiB";
    else if (bytes >= (1ULL << 20)) text << double(bytes) / double(1ULL << 20) << " MiB";
    else if (bytes >= (1ULL << 10)) text << double(bytes) / double(1ULL << 10) << " KiB";
    else text << bytes << " B";
    return text.str();
}

struct DeviceStorage {
    void* pointer = nullptr;
    ~DeviceStorage() { if (pointer) cudaFree(pointer); }
};
struct Event {
    cudaEvent_t handle{};
    Event() { CUDA_CHECK(cudaEventCreate(&handle)); }
    ~Event() { cudaEventDestroy(handle); }
};

template <typename Entry>
__global__ void initialize_table(Entry* table, std::uint64_t entries)
{
    const std::uint64_t first = std::uint64_t(blockIdx.x) * blockDim.x + threadIdx.x;
    const std::uint64_t stride = std::uint64_t(gridDim.x) * blockDim.x;
    for (std::uint64_t i = first; i < entries; i += stride)
        table[i] = static_cast<Entry>(transition(static_cast<std::uint32_t>(i)));
}

template <int Variant, bool Sequential, typename Entry>
__global__ void lookup_fsm(const Entry* table, unsigned iterations, std::uint32_t* output)
{
    const unsigned thread = blockIdx.x * blockDim.x + threadIdx.x;
    const volatile Entry* lookup = table; // Force actual LUT loads in this synthetic test.
    std::uint32_t accumulated = 0;
    for (unsigned i = 0; i < iterations; ++i) {
        std::uint32_t state = input_cube(thread, i);
        if constexpr (Variant == 0) accumulated += lookup[state];
        else {
            [[maybe_unused]] std::uint32_t checksum = 0;
            constexpr unsigned parts = Variant == 1 ? 6u : 12u;
            #pragma unroll
            for (unsigned part = 0; part < parts; ++part) {
                const auto result = std::uint32_t(lookup[extract_subtile(state, Variant, part)]);
                if constexpr (Sequential)
                    state = replace_subtile(state, Variant, part, result);
                else
                    checksum += result * (part + 1u);
            }
            if constexpr (Sequential) accumulated += state;
            else accumulated += checksum;
        }
    }
    output[thread] = accumulated;
}

template <int Variant, bool Sequential, typename Entry>
void benchmark(const Entry* table, unsigned iterations, unsigned blocks,
               unsigned repeats, std::uint32_t* output, std::uint64_t table_bytes)
{
    lookup_fsm<Variant, Sequential><<<blocks, block_threads>>>(table, iterations, output);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    Event start, end;
    std::vector<float> samples;
    for (unsigned r = 0; r < repeats; ++r) {
        CUDA_CHECK(cudaEventRecord(start.handle));
        lookup_fsm<Variant, Sequential><<<blocks, block_threads>>>(table, iterations, output);
        CUDA_CHECK(cudaGetLastError());
        CUDA_CHECK(cudaEventRecord(end.handle));
        CUDA_CHECK(cudaEventSynchronize(end.handle));
        float ms = 0;
        CUDA_CHECK(cudaEventElapsedTime(&ms, start.handle, end.handle));
        samples.push_back(ms);
    }
    // Check first and last warp; CPU uses the same synthetic rules on demand.
    for (unsigned first : {0u, blocks * block_threads - 32u}) {
        std::uint32_t observed[32];
        CUDA_CHECK(cudaMemcpy(observed, output + first, sizeof observed, cudaMemcpyDeviceToHost));
        for (unsigned lane = 0; lane < 32; ++lane) {
            std::uint32_t expected = 0;
            for (unsigned i = 0; i < iterations; ++i)
                expected += reference_update<Variant, Sequential>(input_cube(first + lane, i));
            if (observed[lane] != expected) throw std::runtime_error("CPU/GPU checksum mismatch");
        }
    }
    std::sort(samples.begin(), samples.end());
    const auto n = samples.size();
    const double ms = n % 2 ? samples[n / 2] : (samples[n / 2 - 1] + samples[n / 2]) / 2.;
    const double updates = double(blocks) * block_threads * iterations;
    constexpr unsigned lookups = Variant == 0 ? 1u : Variant == 1 ? 6u : 12u;
    const char* name = Variant == 0 ? "cube" : Variant == 1 ? "faces" : "edges";
    const char* mode = Variant == 0 ? "single" : Sequential ? "sequential_overwrite" : "independent_proposals";
    if (csv_output()) {
        std::cout << name << ',' << mode << ',' << lookups << ',' << sizeof(Entry) << ','
                  << table_bytes << ',' << ms << ',' << updates / (ms * 1000.) << ','
                  << ms * 1e6 / updates << ",PASS\n";
    } else {
        const char* readable_mode = Variant == 0 ? "Single"
            : Sequential ? "Sequential" : "Independent";
        std::cout << std::left << std::setw(9) << name
                  << std::setw(14) << readable_mode
                  << std::right << std::setw(9) << lookups
                  << std::setw(9) << sizeof(Entry)
                  << std::setw(14) << memory_size(table_bytes)
                  << std::fixed << std::setprecision(4)
                  << std::setw(13) << ms
                  << std::setprecision(2)
                  << std::setw(14) << updates / (ms * 1000.)
                  << std::setprecision(3)
                  << std::setw(13) << ms * 1e6 / updates
                  << std::setw(9) << "PASS" << '\n';
    }
}

template <int Variant, typename Entry>
bool run_variant(unsigned iterations, unsigned blocks, unsigned repeats,
                 std::uint32_t* output, const std::string& mode)
{
    constexpr std::uint64_t entries = Variant == 0 ? (1ULL << 32)
        : Variant == 1 ? (1ULL << 16) : (1ULL << 8);
    constexpr std::uint64_t bytes = entries * sizeof(Entry);
    static_assert(sizeof(std::size_t) >= 8, "This benchmark requires a 64-bit host");
    const char* name = Variant == 0 ? "cube" : Variant == 1 ? "faces" : "edges";
    std::size_t free_bytes = 0, total_bytes = 0;
    CUDA_CHECK(cudaMemGetInfo(&free_bytes, &total_bytes));
    if (bytes > free_bytes || free_bytes - bytes < (64ULL << 20)) {
        std::cerr << "SKIP " << name << ": needs " << bytes
                  << " table bytes plus headroom; free=" << free_bytes << '\n';
        return false;
    }
    DeviceStorage storage;
    const auto allocated = cudaMalloc(&storage.pointer, static_cast<std::size_t>(bytes));
    if (allocated == cudaErrorMemoryAllocation) {
        // Surface the skip instead of silently substituting a smaller table.
        (void)cudaGetLastError();
        std::cerr << "SKIP " << name << ": cudaMalloc could not allocate " << bytes << " bytes\n";
        return false;
    }
    CUDA_CHECK(allocated);
    auto* table = static_cast<Entry*>(storage.pointer);
    // Initialize directly on GPU: do not allocate/copy a 16-GiB host table.
    initialize_table<<<4096, block_threads>>>(table, entries);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    if constexpr (Variant == 0)
        benchmark<Variant, false>(table, iterations, blocks, repeats, output, bytes);
    else {
        if (mode != "sequential") benchmark<Variant, false>(table, iterations, blocks, repeats, output, bytes);
        if (mode != "independent") benchmark<Variant, true>(table, iterations, blocks, repeats, output, bytes);
    }
    return true;
}

static unsigned argument(const char* text)
{
    if (*text == '-') throw std::invalid_argument("Counts must be positive");
    std::size_t end = 0;
    const auto value = std::stoull(text, &end);
    if (text[end] || value == 0 || value > 0xffffffffULL)
        throw std::invalid_argument("Invalid count");
    return static_cast<unsigned>(value);
}

int main(int argc, char** argv)
{
    try {
        if (argc > 6) throw std::invalid_argument("Usage: ./fsm_lookup_bench [all|cube|faces|edges] [ITERATIONS] [BLOCKS] [REPEATS] [both|independent|sequential]");
        const std::string variant = argc > 1 ? argv[1] : "all";
        const unsigned iterations = argc > 2 ? argument(argv[2]) : 1000;
        const unsigned blocks = argc > 3 ? argument(argv[3]) : 512;
        const unsigned repeats = argc > 4 ? argument(argv[4]) : 5;
        const std::string mode = argc > 5 ? argv[5] : "both";
        if (variant != "all" && variant != "cube" && variant != "faces" && variant != "edges")
            throw std::invalid_argument("Unknown variant");
        if (mode != "both" && mode != "independent" && mode != "sequential")
            throw std::invalid_argument("Unknown mode");
        if (blocks > 65535) throw std::invalid_argument("At most 65535 blocks supported");
        cudaDeviceProp prop{}; int device = 0;
        CUDA_CHECK(cudaGetDevice(&device));
        CUDA_CHECK(cudaGetDeviceProperties(&prop, device));
        if (csv_output()) {
            std::cout << "# GPU=" << prop.name << "; SMs=" << prop.multiProcessorCount
                      << "; CC=" << prop.major << '.' << prop.minor
                      << "; device_memory_bytes=" << prop.totalGlobalMem
                      << "; iterations=" << iterations << "; blocks=" << blocks
                      << "; threads_per_block=" << block_threads << "; repeats=" << repeats << '\n'
                      << "variant,mode,lookups_per_cube,entry_bytes,table_bytes,median_ms,Mcubes_per_s,aggregate_ns_per_cube,validation\n";
        } else {
            const std::uint64_t inputs = std::uint64_t(blocks) * block_threads * iterations;
            std::cout << "\nFSM lookup benchmark (synthetic transitions)\n"
                      << "GPU:                  " << prop.name << '\n'
                      << "SMs / compute cap.:   " << prop.multiProcessorCount << " / "
                      << prop.major << '.' << prop.minor << '\n'
                      << "GPU memory:           " << memory_size(prop.totalGlobalMem) << '\n'
                      << "Launch:               " << blocks << " blocks x " << block_threads << " threads\n"
                      << "Inputs per thread:    " << iterations << '\n'
                      << "Cube inputs per run:  " << inputs << '\n'
                      << "Timed repetitions:    " << repeats << " (after warmup)\n\n"
                      << std::left << std::setw(9) << "Variant" << std::setw(14) << "Mode"
                      << std::right << std::setw(9) << "Lookups"
                      << std::setw(9) << "Entry(B)" << std::setw(14) << "Table size"
                      << std::setw(13) << "Time(ms)" << std::setw(14) << "Mcubes/s"
                      << std::setw(13) << "ns/cube" << std::setw(9) << "Check" << '\n';
        }
        DeviceStorage output_storage;
        CUDA_CHECK(cudaMalloc(&output_storage.pointer, std::size_t(blocks) * block_threads * sizeof(std::uint32_t)));
        auto* output = static_cast<std::uint32_t*>(output_storage.pointer);
        bool complete = true;
        // Allocate one table at a time. The full cube table alone is 16 GiB.
        if (variant == "all" || variant == "edges") complete &= run_variant<2, std::uint8_t>(iterations, blocks, repeats, output, mode);
        if (variant == "all" || variant == "faces") complete &= run_variant<1, std::uint16_t>(iterations, blocks, repeats, output, mode);
        if (variant == "all" || variant == "cube") complete &= run_variant<0, std::uint32_t>(iterations, blocks, repeats, output, mode);
        if (!csv_output()) {
            std::cout << "\nTime(ms): median kernel time. Mcubes/s: million cube inputs per second.\n"
                      << "ns/cube: reciprocal aggregate throughput, not individual lookup latency.\n"
                      << "Independent: original-state proposals; no conflict resolution.\n"
                      << "Sequential: ordered updates to a temporary cube.\n"
                      << "PASS: first/last warp checksums agree with the CPU reference.\n";
        }
        return complete ? 0 : 2; // 2 means at least one requested variant skipped.
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
