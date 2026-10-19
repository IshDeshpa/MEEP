#include "benchmark_common.h"

#include <cuda_runtime.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

void cuda_check(cudaError_t status, const char* expression, int line) {
    if (status != cudaSuccess)
        throw std::runtime_error(std::string(expression) + " at line " +
                                 std::to_string(line) + ": " + cudaGetErrorString(status));
}
#define CUDA_CHECK(expression) cuda_check((expression), #expression, __LINE__)

template <bool Lookup, bool OnePerTile>
__global__ void update(const uint32_t* __restrict__ current,
                       uint32_t* __restrict__ next,
                       const uint32_t* __restrict__ table, uint64_t tiles) {
    const uint64_t thread = uint64_t{blockIdx.x} * blockDim.x + threadIdx.x;
    if constexpr (OnePerTile) {
        if (thread < tiles) {
            const uint32_t state = current[thread];
            if constexpr (Lookup) {
                // The LUT address depends on the world load, and the store
                // payload depends on the LUT load. Preserve this full chain.
                const uint32_t next_state = table[state];
                next[thread] = next_state;
            } else {
                next[thread] = state;  // Optional buffer-only copy control.
            }
        }
    } else {
        const uint64_t stride = uint64_t{gridDim.x} * blockDim.x;
        for (uint64_t tile = thread; tile < tiles; tile += stride) {
            const uint32_t state = current[tile];
            if constexpr (Lookup) {
                const uint32_t next_state = table[state];
                next[tile] = next_state;
            } else {
                next[tile] = state;  // Optional buffer-only copy control.
            }
        }
    }
}

__global__ void initialize_table(uint32_t* table, meep::StateSpace space) {
    const uint64_t stride = uint64_t{gridDim.x} * blockDim.x;
    for (uint64_t state = uint64_t{blockIdx.x} * blockDim.x + threadIdx.x;
         state < space.count; state += stride)
        table[state] = meep::transition(static_cast<uint32_t>(state), space);
}

struct Options {
    uint64_t x = 100, y = 100, z = 100, seed = 42;
    unsigned materials = 8, ticks = 10, warmup = 2, repeats = 3, device = 0;
    uint64_t min_workers = 1;
    std::vector<unsigned> block_sizes{256};
    std::vector<uint64_t> workers;  // Empty means an automatic sweep.
    std::string access = "lookup", launch = "graph", scheduler = "both", output = "results/latest.csv";
    bool verify = true;
};

uint64_t positive_integer(const std::string& value, bool allow_zero = false) {
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("expected an unsigned integer, got: " + value);
    size_t parsed = 0;
    const uint64_t result = std::stoull(value, &parsed);
    if (parsed != value.size() || (!allow_zero && !result))
        throw std::invalid_argument("expected a positive integer, got: " + value);
    return result;
}

unsigned small_integer(const std::string& value, bool allow_zero = false) {
    const uint64_t result = positive_integer(value, allow_zero);
    if (result > std::numeric_limits<unsigned>::max())
        throw std::invalid_argument("integer exceeds 32 bits: " + value);
    return static_cast<unsigned>(result);
}

std::vector<uint64_t> integer_list(const std::string& value) {
    std::vector<uint64_t> result;
    std::stringstream input(value);
    std::string part;
    while (std::getline(input, part, ',')) result.push_back(positive_integer(part));
    if (result.empty() || value.back() == ',')
        throw std::invalid_argument("expected a comma-separated list of positive integers");
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

void print_help() {
    std::cout <<
        "CUDA neighborhood thread-count benchmark\n"
        "Usage: thread_count [options]\n"
        "  --world X Y Z          Even voxel dimensions (default 100 100 100)\n"
        "  --materials N          Any count from 2 through 16 (default 8)\n"
        "  --block-sizes LIST     Comma-separated block sizes (default 256)\n"
        "  --workers LIST         Requested grid-stride threads; default auto sweep\n"
        "  --min-workers N        Lower bound for automatic sweep (default 1)\n"
        "  --scheduler MODE       grid_stride, one_per_tile, or both (default both)\n"
        "  --ticks N              Timed ping-pong ticks per sample (default 10)\n"
        "  --warmup N             Untimed ticks per sample; may be 0 (default 2)\n"
        "  --repeats N            Samples per configuration (default 3)\n"
        "  --access MODE          lookup, copy, or both (default lookup)\n"
        "                         lookup: world read -> LUT read -> next-buffer write\n"
        "                         copy: optional buffer-only control, without a LUT\n"
        "  --launch MODE          graph or direct (default graph)\n"
        "  --seed N               Initial-state seed (default 42)\n"
        "  --device N             CUDA device index (default 0)\n"
        "  --output PATH          CSV path or - for stdout (default results/latest.csv)\n"
        "  --no-verify            Skip full final-state checks (default: verify)\n"
        "  --help                 Print this help without requiring a GPU\n"
        "By default each block size also runs a one-thread-per-tile kernel.\n";
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        auto next = [&]() -> std::string {
            if (++i >= argc) throw std::invalid_argument("missing value for " + flag);
            return argv[i];
        };
        if (flag == "--world") {
            options.x = positive_integer(next());
            options.y = positive_integer(next());
            options.z = positive_integer(next());
        } else if (flag == "--materials") options.materials = small_integer(next());
        else if (flag == "--ticks") options.ticks = small_integer(next());
        else if (flag == "--warmup") options.warmup = small_integer(next(), true);
        else if (flag == "--repeats") options.repeats = small_integer(next());
        else if (flag == "--seed") options.seed = positive_integer(next(), true);
        else if (flag == "--device") options.device = small_integer(next(), true);
        else if (flag == "--access") options.access = next();
        else if (flag == "--launch") options.launch = next();
        else if (flag == "--scheduler") options.scheduler = next();
        else if (flag == "--output") options.output = next();
        else if (flag == "--workers") options.workers = integer_list(next());
        else if (flag == "--min-workers") options.min_workers = positive_integer(next());
        else if (flag == "--no-verify") options.verify = false;
        else if (flag == "--block-sizes") {
            options.block_sizes.clear();
            for (uint64_t block : integer_list(next())) {
                if (block > 1024) throw std::invalid_argument("block size exceeds 1024");
                options.block_sizes.push_back(static_cast<unsigned>(block));
            }
        } else throw std::invalid_argument("unknown option: " + flag);
    }
    meep::tile_count(options.x, options.y, options.z);
    meep::state_count(options.materials);
    if (options.device > static_cast<unsigned>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("device index is too large");
    if (options.access != "lookup" && options.access != "copy" && options.access != "both")
        throw std::invalid_argument("access must be lookup, copy, or both");
    if (options.launch != "graph" && options.launch != "direct")
        throw std::invalid_argument("launch must be graph or direct");
    if (options.scheduler != "both" && options.scheduler != "grid_stride" &&
        options.scheduler != "one_per_tile")
        throw std::invalid_argument("scheduler must be grid_stride, one_per_tile, or both");
    if (options.output.empty()) throw std::invalid_argument("empty output path");
    return options;
}

struct DeviceBuffer {
    uint32_t* data = nullptr;
    explicit DeviceBuffer(uint64_t bytes) {
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&data), static_cast<size_t>(bytes)));
    }
    ~DeviceBuffer() { if (data) cudaFree(data); }
    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;
};

struct Stream {
    cudaStream_t value = nullptr;
    Stream() { CUDA_CHECK(cudaStreamCreateWithFlags(&value, cudaStreamNonBlocking)); }
    ~Stream() { if (value) cudaStreamDestroy(value); }
};

struct Event {
    cudaEvent_t value = nullptr;
    Event() { CUDA_CHECK(cudaEventCreate(&value)); }
    ~Event() { if (value) cudaEventDestroy(value); }
};

struct Graph {
    cudaGraph_t graph = nullptr;
    cudaGraphExec_t executable = nullptr;
    ~Graph() {
        if (executable) cudaGraphExecDestroy(executable);
        if (graph) cudaGraphDestroy(graph);
    }
};

struct DeviceInfo {
    cudaDeviceProp properties{};
    int driver_version = 0, runtime_version = 0;
    uint64_t cuda_total_vram_before_alloc = 0, cuda_free_vram_before_alloc = 0;
    uint64_t required_vram = 0;
};

template <bool Lookup, bool OnePerTile>
int active_blocks(unsigned block_size) {
    int count = 0;
    CUDA_CHECK(cudaOccupancyMaxActiveBlocksPerMultiprocessor(
        &count, update<Lookup, OnePerTile>, block_size, 0));
    if (!count) throw std::runtime_error("kernel has zero predicted occupancy");
    return count;
}

std::string csv_string(const std::string& value) {
    std::string result = "\"";
    for (char character : value) {
        if (character == '"') result += '"';
        result += character;
    }
    return result + '"';
}

void csv_header(std::ostream& output) {
    output << "gpu,compute_capability,driver_version,runtime_version,sm_count,warp_size,"
              "max_threads_per_sm,l2_bytes,total_vram_bytes,cuda_total_vram_bytes_before_alloc,"
              "cuda_free_vram_bytes_before_alloc,cuda_used_vram_bytes_before_alloc,"
              "required_vram_bytes,world_x,world_y,world_z,tiles,"
              "materials,lut_bytes,seed,access,scheduler,launch_mode,requested_block_size,"
              "requested_workers,threads_per_block,blocks,launched_threads,working_threads,"
              "kernel_registers,active_blocks_per_sm,resident_thread_capacity,"
              "warp_occupancy_limit,grid_to_capacity_ratio,max_tiles_per_thread,"
              "warmup_ticks,timed_ticks,repeats,min_ms_per_tick,median_ms_per_tick,"
              "p90_ms_per_tick,max_ms_per_tick,tiles_per_second,logical_gb_per_second,"
              "verified,checksum,sample_ms_per_tick\n";
}

template <bool Lookup, bool OnePerTile>
void benchmark_configuration(const Options& options, const DeviceInfo& info,
                             unsigned requested_block, uint64_t requested_workers,
                             const meep::Launch& launch, uint64_t tiles,
                             uint64_t lut_bytes, const std::vector<uint32_t>& initial,
                             const std::vector<uint32_t>& expected, DeviceBuffer& world_a,
                             DeviceBuffer& world_b, const uint32_t* table,
                             cudaStream_t stream, std::ostream& output) {
    const int active = active_blocks<Lookup, OnePerTile>(launch.block_size);
    const uint64_t resident = uint64_t{static_cast<unsigned>(active)} *
                              launch.block_size * info.properties.multiProcessorCount;
    const double occupancy = double(active * meep::ceil_div(launch.block_size,
                                                           info.properties.warpSize)) /
        (info.properties.maxThreadsPerMultiProcessor / info.properties.warpSize);
    cudaFuncAttributes attributes{};
    CUDA_CHECK(cudaFuncGetAttributes(&attributes, update<Lookup, OnePerTile>));
    Event begin, end;
    Graph graph;
    auto launch_tick = [&](uint32_t*& current, uint32_t*& next) {
        update<Lookup, OnePerTile><<<static_cast<unsigned>(launch.blocks),
                                    launch.block_size, 0, stream>>>(current, next, table, tiles);
        std::swap(current, next);
    };

    if (options.launch == "graph") {
        // Warmup parity is identical for every reset/sample. The graph encodes
        // a sequence of separate kernels; stream order is the tick barrier.
        uint32_t* current = (options.warmup % 2) ? world_b.data : world_a.data;
        uint32_t* next = (options.warmup % 2) ? world_a.data : world_b.data;
        CUDA_CHECK(cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal));
        for (unsigned tick = 0; tick < options.ticks; ++tick) launch_tick(current, next);
        CUDA_CHECK(cudaStreamEndCapture(stream, &graph.graph));
        CUDA_CHECK(cudaGraphInstantiate(&graph.executable, graph.graph, nullptr, nullptr, 0));
        CUDA_CHECK(cudaGraphUpload(graph.executable, stream));
        CUDA_CHECK(cudaStreamSynchronize(stream));
    }

    std::vector<double> samples;
    samples.reserve(options.repeats);
    for (unsigned repeat = 0; repeat < options.repeats; ++repeat) {
        CUDA_CHECK(cudaMemcpyAsync(world_a.data, initial.data(), initial.size() * sizeof(uint32_t),
                                   cudaMemcpyHostToDevice, stream));
        uint32_t* current = world_a.data;
        uint32_t* next = world_b.data;
        for (unsigned tick = 0; tick < options.warmup; ++tick) launch_tick(current, next);
        CUDA_CHECK(cudaGetLastError());
        CUDA_CHECK(cudaStreamSynchronize(stream));
        CUDA_CHECK(cudaEventRecord(begin.value, stream));
        if (options.launch == "graph") {
            CUDA_CHECK(cudaGraphLaunch(graph.executable, stream));
            if (options.ticks % 2) std::swap(current, next);
        } else {
            for (unsigned tick = 0; tick < options.ticks; ++tick) launch_tick(current, next);
            CUDA_CHECK(cudaGetLastError());
        }
        CUDA_CHECK(cudaEventRecord(end.value, stream));
        CUDA_CHECK(cudaEventSynchronize(end.value));
        float elapsed_ms = 0;
        CUDA_CHECK(cudaEventElapsedTime(&elapsed_ms, begin.value, end.value));
        if (!(elapsed_ms > 0)) throw std::runtime_error("timed interval too short; increase --ticks");
        samples.push_back(double(elapsed_ms) / options.ticks);

        // Check every sample, including odd and even tick sequences. Transfers
        // and the complete CPU comparison happen after the stop event.
        if (options.verify) {
            std::vector<uint32_t> observed(tiles);
            CUDA_CHECK(cudaMemcpyAsync(observed.data(), current, observed.size() * sizeof(uint32_t),
                                       cudaMemcpyDeviceToHost, stream));
            CUDA_CHECK(cudaStreamSynchronize(stream));
            const auto mismatch = std::mismatch(observed.begin(), observed.end(), expected.begin());
            if (mismatch.first != observed.end())
                throw std::runtime_error("verification failed at tile " +
                    std::to_string(mismatch.first - observed.begin()) + " (repeat " +
                    std::to_string(repeat) + ")");
        }
    }

    const double median = meep::percentile(samples, 0.5);
    const double tiles_per_second = double(tiles) * 1000 / median;
    const auto& properties = info.properties;
    std::ostringstream sample_text;
    sample_text << std::setprecision(9);
    for (size_t i = 0; i < samples.size(); ++i) {
        if (i) sample_text << ';';
        sample_text << samples[i];
    }
    output << std::setprecision(12)
           << csv_string(properties.name) << ',' << properties.major << '.' << properties.minor << ','
           << info.driver_version << ',' << info.runtime_version << ','
           << properties.multiProcessorCount << ',' << properties.warpSize << ','
           << properties.maxThreadsPerMultiProcessor << ',' << properties.l2CacheSize << ','
           << properties.totalGlobalMem << ',' << info.cuda_total_vram_before_alloc << ','
           << info.cuda_free_vram_before_alloc << ','
           << info.cuda_total_vram_before_alloc - info.cuda_free_vram_before_alloc << ','
           << info.required_vram << ',' << options.x << ',' << options.y << ',' << options.z << ','
           << tiles << ',' << options.materials << ',' << lut_bytes << ',' << options.seed << ','
           << (Lookup ? "lookup" : "copy") << ',' << (OnePerTile ? "one_per_tile" : "grid_stride") << ','
           << options.launch << ',' << requested_block << ',' << requested_workers << ','
           << launch.block_size << ',' << launch.blocks << ',' << launch.threads << ','
           << std::min(tiles, launch.threads) << ',' << attributes.numRegs << ',' << active << ','
           << resident << ',' << occupancy << ',' << double(launch.threads) / resident << ','
           << meep::ceil_div(tiles, launch.threads) << ',' << options.warmup << ',' << options.ticks << ','
           << options.repeats << ',' << meep::percentile(samples, 0) << ',' << median << ','
           << meep::percentile(samples, 0.9) << ',' << meep::percentile(samples, 1) << ','
           << tiles_per_second << ',' << tiles_per_second * (Lookup ? 12 : 8) / 1e9 << ','
           << (options.verify ? "true" : "false") << ',';
    if (options.verify) output << meep::checksum(expected);
    output << ',' << csv_string(sample_text.str()) << '\n';
    output.flush();
    if (!output) throw std::runtime_error("failed to write result CSV");
    std::cerr << std::setw(6) << (Lookup ? "lookup" : "copy") << ' '
              << std::setw(12) << (OnePerTile ? "one_per_tile" : "grid_stride")
              << " block=" << std::setw(4) << launch.block_size
              << " threads=" << std::setw(9) << launch.threads
              << " capacity=" << std::setw(7) << resident
              << " median=" << std::fixed << std::setprecision(6) << median << " ms/tick\n";
}

template <bool Lookup>
void sweep(const Options& options, const DeviceInfo& info, uint64_t tiles,
           uint64_t lut_bytes, const std::vector<uint32_t>& initial,
           DeviceBuffer& world_a, DeviceBuffer& world_b, const uint32_t* table,
           cudaStream_t stream, std::ostream& output) {
    auto expected = initial;
    if (options.verify && Lookup) {
        const auto space = meep::state_space(options.materials);
        const uint64_t total_ticks = uint64_t{options.warmup} + options.ticks;
        for (uint64_t tick = 0; tick < total_ticks; ++tick)
            for (auto& state : expected) state = meep::transition(state, space);
    }
    for (unsigned block : options.block_sizes) {
        if (options.scheduler != "one_per_tile") {
            const uint64_t capacity = uint64_t{static_cast<unsigned>(active_blocks<Lookup, false>(block))} *
                                     block * info.properties.multiProcessorCount;
            const auto workers = options.workers.empty() ? meep::sweep_workers(tiles, capacity) : options.workers;
            std::set<std::pair<uint64_t, unsigned>> visited;
            for (uint64_t requested : workers) {
                if (options.workers.empty() && requested < options.min_workers) continue;
                const auto launch = meep::launch_for(requested, block);
                if (!visited.emplace(launch.blocks, launch.block_size).second) continue;
                benchmark_configuration<Lookup, false>(options, info, block, requested, launch, tiles,
                    lut_bytes, initial, expected, world_a, world_b, table, stream, output);
            }
        }
        if (options.scheduler != "grid_stride") {
            const auto launch = meep::launch_for(tiles, block);
            benchmark_configuration<Lookup, true>(options, info, block, tiles, launch, tiles,
                lut_bytes, initial, expected, world_a, world_b, table, stream, output);
        }
    }
}

void run(const Options& options) {
    const uint64_t tiles = meep::tile_count(options.x, options.y, options.z);
    const auto space = meep::state_space(options.materials);
    const uint64_t states = space.count;
    const uint64_t world_bytes = meep::checked_product(tiles, sizeof(uint32_t));
    const uint64_t lut_bytes = options.access == "copy" ? 0 : states * sizeof(uint32_t);
    const uint64_t worlds_bytes = meep::checked_product(world_bytes, 2);
    if (lut_bytes > UINT64_MAX - worlds_bytes) throw std::invalid_argument("allocation size overflow");
    const uint64_t needed = worlds_bytes + lut_bytes;
    if (needed > SIZE_MAX) throw std::invalid_argument("allocation exceeds host address space");
    DeviceInfo info;
    CUDA_CHECK(cudaSetDevice(static_cast<int>(options.device)));
    CUDA_CHECK(cudaGetDeviceProperties(&info.properties, static_cast<int>(options.device)));
    CUDA_CHECK(cudaDriverGetVersion(&info.driver_version));
    CUDA_CHECK(cudaRuntimeGetVersion(&info.runtime_version));
    size_t free_bytes = 0, total_bytes = 0;
    CUDA_CHECK(cudaMemGetInfo(&free_bytes, &total_bytes));
    info.cuda_total_vram_before_alloc = total_bytes;
    info.cuda_free_vram_before_alloc = free_bytes;
    info.required_vram = needed;
    std::cerr << "VRAM preflight: required " << needed << " bytes, free " << free_bytes
              << ", used/unavailable " << total_bytes - free_bytes << ", total " << total_bytes << '\n';
    if (needed > free_bytes)
        throw std::runtime_error("device memory required: " + std::to_string(needed) +
            " bytes (LUT " + std::to_string(lut_bytes) + "), free: " +
            std::to_string(free_bytes) + ". Reduce world/materials; 16 materials need a 16 GiB LUT.");
    for (unsigned block : options.block_sizes) {
        if (block > static_cast<unsigned>(info.properties.maxThreadsPerBlock) ||
            block > static_cast<unsigned>(info.properties.maxThreadsDim[0]))
            throw std::invalid_argument("block size exceeds device limit");
        // Validate every explicit request, as well as the one-per-tile grid,
        // before allocating buffers or producing a partial measurement file.
        if (meep::launch_for(tiles, block).blocks > uint64_t{static_cast<unsigned>(info.properties.maxGridSize[0])})
            throw std::invalid_argument("world exceeds the one-dimensional grid limit");
        for (uint64_t workers : options.workers)
            if (meep::launch_for(workers, block).blocks > uint64_t{static_cast<unsigned>(info.properties.maxGridSize[0])})
                throw std::invalid_argument("workers exceed the one-dimensional grid limit");
    }

    std::cerr << "GPU: " << info.properties.name << " (sm_" << info.properties.major
              << info.properties.minor << "), " << info.properties.multiProcessorCount << " SMs, "
              << info.properties.maxThreadsPerMultiProcessor << " max threads/SM, L2 "
              << info.properties.l2CacheSize << " bytes\nWorld: " << options.x << 'x' << options.y << 'x'
              << options.z << ", " << tiles << " tiles, LUT " << lut_bytes << " bytes, buffers "
              << worlds_bytes << " bytes\nMode: " << options.launch << ", " << options.ticks
              << " timed ticks, " << options.repeats << " samples, verification "
              << (options.verify ? "enabled" : "disabled") << '\n';
    std::vector<uint32_t> initial(tiles);
    for (uint64_t tile = 0; tile < tiles; ++tile)
        initial[tile] = meep::initial_state(tile, options.seed, space);
    DeviceBuffer world_a(world_bytes), world_b(world_bytes);
    Stream stream;
    // cudaMalloc(0) is permitted, but no allocation is needed for copy-only runs.
    DeviceBuffer table(lut_bytes);
    if (lut_bytes) {
        const auto blocks = static_cast<unsigned>(std::min(uint64_t{65535}, meep::ceil_div(states, 256)));
        initialize_table<<<blocks, 256, 0, stream.value>>>(table.data, space);
        CUDA_CHECK(cudaGetLastError());
        CUDA_CHECK(cudaStreamSynchronize(stream.value));
    }

    std::ofstream file;
    std::ostream* output = &std::cout;
    if (options.output != "-") {
        const auto path = std::filesystem::path(options.output);
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        file.open(path, std::ios::trunc);
        if (!file) throw std::runtime_error("cannot open output: " + options.output);
        output = &file;
    }
    csv_header(*output);
    if (options.access != "copy")
        sweep<true>(options, info, tiles, lut_bytes, initial, world_a, world_b, table.data, stream.value, *output);
    if (options.access != "lookup")
        sweep<false>(options, info, tiles, lut_bytes, initial, world_a, world_b, table.data, stream.value, *output);
    std::cerr << "Completed. CSV: " << options.output << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        for (int i = 1; i < argc; ++i)
            if (std::string(argv[i]) == "--help") { print_help(); return 0; }
        run(parse_options(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
