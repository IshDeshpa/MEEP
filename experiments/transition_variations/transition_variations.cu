#include "transitions.h"
#include "reference_world.h"

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

template <tv::Shape Kind, bool Sequential>
__global__ void update(const uint32_t* __restrict__ current,
                       uint32_t* __restrict__ next,
                       const typename tv::Traits<Kind>::Entry* __restrict__ table,
                       uint64_t tiles, unsigned materials) {
    using Entry = typename tv::Traits<Kind>::Entry;
    const tv::TableLookup<Entry, Kind != tv::Shape::cube && !Sequential> lookup{table};
    const uint64_t thread = uint64_t{blockIdx.x} * blockDim.x + threadIdx.x;
    if (thread < tiles) {
        const uint32_t state = current[thread];
        const uint32_t result = tv::update_tile<Kind, Sequential>(state, materials, lookup);
        next[thread] = result;
    }
}

template <class Entry>
__global__ void initialize_table(Entry* table, meep::StateSpace space) {
    const uint64_t stride = uint64_t{gridDim.x} * blockDim.x;
    for (uint64_t state = uint64_t{blockIdx.x} * blockDim.x + threadIdx.x;
         state < space.count; state += stride)
        table[state] = static_cast<Entry>(meep::transition(static_cast<uint32_t>(state), space));
}

// Separate name lets Nsight exclude all preheating while matching only update.
template <tv::Shape Kind, bool Sequential>
__global__ void heat_update(const uint32_t* __restrict__ current,
                            uint32_t* __restrict__ next,
                            const typename tv::Traits<Kind>::Entry* __restrict__ table,
                            uint64_t tiles, unsigned materials) {
    using Entry = typename tv::Traits<Kind>::Entry;
    const tv::TableLookup<Entry, Kind != tv::Shape::cube && !Sequential> lookup{table};
    const uint64_t tile = uint64_t{blockIdx.x} * blockDim.x + threadIdx.x;
    if (tile < tiles)
        next[tile] = tv::update_tile<Kind, Sequential>(current[tile], materials, lookup);
}

struct Options {
    uint64_t x = 100, y = 100, z = 100, seed = 42;
    unsigned materials = 8, ticks = 10, warmup = 2, repeats = 3, device = 0;
    unsigned block_size = 256;
    unsigned preheat_ms = 0;
    std::string variant = "all", mode = "both", launch = "graph";
    std::string output = "results/latest.csv";
    bool verify = true, instrumented = false;
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

void print_help() {
    std::cout <<
        "CUDA transition-variation benchmark (one thread per tile)\n"
        "Usage: transition_variations [options]\n"
        "  --world X Y Z          Even voxel dimensions (default 100 100 100)\n"
        "  --materials N          Any count from 2 through 16 (default 8)\n"
        "  --block-size N         Threads per block, fixed for the run (default 256)\n"
        "  --ticks N              Timed ping-pong ticks per sample (default 10)\n"
        "  --warmup N             Untimed ticks per sample; may be 0 (default 2)\n"
        "  --repeats N            Samples per configuration (default 3)\n"
        "  --preheat-ms N         Untimed GPU preheating before each sample (default 0)\n"
        "  --variant NAME         cube, face, edge, or all (default all)\n"
        "  --mode MODE            sequential, independent, or both (default both)\n"
        "                         Cube always uses a single lookup.\n"
        "                         Independent: original input, fixed-order overwrite.\n"
        "  --launch MODE          graph or direct (default graph)\n"
        "  --seed N               Initial-state seed (default 42)\n"
        "  --device N             CUDA device index (default 0)\n"
        "  --output PATH          CSV path or - for stdout (default results/latest.csv)\n"
        "  --no-verify            Skip full final-state checks (default: verify)\n"
        "  --instrumented         Label timings from profiler/sanitizer runs\n"
        "  --help                 Print this help without requiring a GPU\n"
        "Each update kernel launches one thread per tile, with padding in the final block.\n";
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
        else if (flag == "--preheat-ms") options.preheat_ms = small_integer(next(), true);
        else if (flag == "--seed") options.seed = positive_integer(next(), true);
        else if (flag == "--device") options.device = small_integer(next(), true);
        else if (flag == "--variant") options.variant = next();
        else if (flag == "--mode") options.mode = next();
        else if (flag == "--launch") options.launch = next();
        else if (flag == "--output") options.output = next();
        else if (flag == "--no-verify") options.verify = false;
        else if (flag == "--instrumented") options.instrumented = true;
        else if (flag == "--block-size") options.block_size = small_integer(next());
        else throw std::invalid_argument("unknown option: " + flag);
    }
    meep::tile_count(options.x, options.y, options.z);
    meep::state_count(options.materials);
    if (options.device > static_cast<unsigned>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("device index is too large");
    if (options.variant != "all" && options.variant != "cube" &&
        options.variant != "face" && options.variant != "edge")
        throw std::invalid_argument("variant must be cube, face, edge, or all");
    if (options.mode != "both" && options.mode != "sequential" && options.mode != "independent")
        throw std::invalid_argument("mode must be sequential, independent, or both");
    if (options.launch != "graph" && options.launch != "direct")
        throw std::invalid_argument("launch must be graph or direct");
    if (options.block_size > 1024) throw std::invalid_argument("block size exceeds 1024");
    if (options.output.empty()) throw std::invalid_argument("empty output path");
    return options;
}

template <class Entry>
struct DeviceBuffer {
    Entry* data = nullptr;
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

template <tv::Shape Kind, bool Sequential>
int active_blocks(unsigned block_size) {
    int count = 0;
    CUDA_CHECK(cudaOccupancyMaxActiveBlocksPerMultiprocessor(
        &count, update<Kind, Sequential>, block_size, 0));
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
    output << "gpu,device_index,compute_capability,driver_version,runtime_version,sm_count,warp_size,"
              "max_threads_per_sm,l2_bytes,total_vram_bytes,cuda_total_vram_bytes_before_alloc,"
              "cuda_free_vram_bytes_before_alloc,cuda_used_vram_bytes_before_alloc,"
              "required_vram_bytes,world_x,world_y,world_z,tiles,"
              "materials,lut_entries,entry_bytes,lut_bytes,lookups_per_tile,seed,variant,mode,"
              "independent_merge,scheduler,launch_mode,timing_context,threads_per_block,blocks,launched_threads,working_threads,"
              "kernel_registers,kernel_local_bytes,kernel_shared_bytes,active_blocks_per_sm,resident_thread_capacity,"
              "warp_occupancy_limit,grid_to_capacity_ratio,"
              "warmup_ticks,timed_ticks,repeats,preheat_ms,min_ms_per_tick,median_ms_per_tick,"
              "p90_ms_per_tick,max_ms_per_tick,ticks_per_second,tiles_per_second,logical_gb_per_second,"
              "verified,checksum,sample_ms_per_tick\n";
}

template <tv::Shape Kind, bool Sequential>
void benchmark_configuration(const Options& options, const DeviceInfo& info,
                             const meep::Launch& launch, uint64_t tiles,
                             uint64_t lut_bytes, const std::vector<uint32_t>& initial,
                             const std::vector<uint32_t>& expected, DeviceBuffer<uint32_t>& world_a,
                             DeviceBuffer<uint32_t>& world_b, const typename tv::Traits<Kind>::Entry* table,
                             cudaStream_t stream, std::ostream& output) {
    const int active = active_blocks<Kind, Sequential>(launch.block_size);
    const uint64_t resident = uint64_t{static_cast<unsigned>(active)} *
                              launch.block_size * info.properties.multiProcessorCount;
    const double occupancy = double(active * meep::ceil_div(launch.block_size,
                                                           info.properties.warpSize)) /
        (info.properties.maxThreadsPerMultiProcessor / info.properties.warpSize);
    cudaFuncAttributes attributes{};
    CUDA_CHECK(cudaFuncGetAttributes(&attributes, update<Kind, Sequential>));
    Event begin, end;
    Graph graph;
    auto launch_tick = [&](uint32_t*& current, uint32_t*& next) {
        update<Kind, Sequential><<<static_cast<unsigned>(launch.blocks),
                                    launch.block_size, 0, stream>>>(current, next, table, tiles, options.materials);
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
        if (options.preheat_ms) {
            const auto start = std::chrono::steady_clock::now();
            do {
                for (unsigned tick = 0; tick < 8; ++tick) {
                    heat_update<Kind, Sequential><<<static_cast<unsigned>(launch.blocks),
                        launch.block_size, 0, stream>>>(current, next, table, tiles, options.materials);
                    std::swap(current, next);
                }
                CUDA_CHECK(cudaGetLastError());
                CUDA_CHECK(cudaStreamSynchronize(stream));
            } while (std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - start).count() < options.preheat_ms);
            // Restore the exact native initial state and tick trajectory.
            CUDA_CHECK(cudaMemcpyAsync(world_a.data, initial.data(), initial.size() * sizeof(uint32_t),
                                       cudaMemcpyHostToDevice, stream));
            current = world_a.data;
            next = world_b.data;
        }
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
           << csv_string(properties.name) << ',' << options.device << ','
           << properties.major << '.' << properties.minor << ','
           << info.driver_version << ',' << info.runtime_version << ','
           << properties.multiProcessorCount << ',' << properties.warpSize << ','
           << properties.maxThreadsPerMultiProcessor << ',' << properties.l2CacheSize << ','
           << properties.totalGlobalMem << ',' << info.cuda_total_vram_before_alloc << ','
           << info.cuda_free_vram_before_alloc << ','
           << info.cuda_total_vram_before_alloc - info.cuda_free_vram_before_alloc << ','
           << info.required_vram << ',' << options.x << ',' << options.y << ',' << options.z << ','
           << tiles << ',' << options.materials << ','
           << lut_bytes / sizeof(typename tv::Traits<Kind>::Entry) << ','
           << sizeof(typename tv::Traits<Kind>::Entry) << ',' << lut_bytes << ','
           << tv::Traits<Kind>::lookups << ',' << options.seed << ',' << tv::shape_name(Kind) << ','
           << tv::mode_name<Kind, Sequential>() << ','
           << (Kind != tv::Shape::cube && !Sequential ? "fixed_order_overwrite" : "n/a") << ','
           << "one_per_tile" << ',' << options.launch << ','
           << (options.instrumented ? "instrumented" : "uninstrumented") << ','
           << launch.block_size << ',' << launch.blocks << ',' << launch.threads << ','
           << std::min(tiles, launch.threads) << ',' << attributes.numRegs << ','
           << attributes.localSizeBytes << ',' << attributes.sharedSizeBytes << ',' << active << ','
           << resident << ',' << occupancy << ',' << double(launch.threads) / resident << ','
           << options.warmup << ',' << options.ticks << ','
           << options.repeats << ',' << options.preheat_ms << ',' << meep::percentile(samples, 0) << ',' << median << ','
           << meep::percentile(samples, 0.9) << ',' << meep::percentile(samples, 1) << ','
           << 1000 / median << ',' << tiles_per_second << ','
           << tiles_per_second * (8 + tv::Traits<Kind>::lookups * sizeof(typename tv::Traits<Kind>::Entry)) / 1e9 << ','
           << (options.verify ? "true" : "false") << ',';
    if (options.verify) output << meep::checksum(expected);
    output << ',' << csv_string(sample_text.str()) << '\n';
    output.flush();
    if (!output) throw std::runtime_error("failed to write result CSV");
    std::cerr << std::setw(4) << tv::shape_name(Kind) << ' '
              << std::setw(11) << tv::mode_name<Kind, Sequential>() << ' '
              << "one_per_tile"
              << " block=" << std::setw(4) << launch.block_size
              << " threads=" << std::setw(9) << launch.threads
              << " capacity=" << std::setw(7) << resident
              << " median=" << std::fixed << std::setprecision(6) << median << " ms/tick\n";
}

template <tv::Shape Kind, bool Sequential>
void benchmark_variant(const Options& options, const DeviceInfo& info, uint64_t tiles,
           uint64_t lut_bytes, const std::vector<uint32_t>& initial,
           DeviceBuffer<uint32_t>& world_a, DeviceBuffer<uint32_t>& world_b, const typename tv::Traits<Kind>::Entry* table,
           cudaStream_t stream, std::ostream& output) {
    std::vector<uint32_t> expected;
    if (options.verify) {
        expected = initial;
        const uint64_t total_ticks = uint64_t{options.warmup} + options.ticks;
        tv::reference_world(expected, options.materials, Kind, Sequential, total_ticks);
    }
    const uint64_t blocks = meep::ceil_div(tiles, options.block_size);
    const meep::Launch launch{options.block_size, blocks,
                              meep::checked_product(blocks, options.block_size)};
    benchmark_configuration<Kind, Sequential>(options, info, launch, tiles,
        lut_bytes, initial, expected, world_a, world_b, table, stream, output);
}

template <tv::Shape Kind>
void run_shape(const Options& options, DeviceInfo info, std::ostream& output) {
    using Entry = typename tv::Traits<Kind>::Entry;
    const uint64_t tiles = meep::tile_count(options.x, options.y, options.z);
    const auto space = tv::state_space(options.materials, tv::Traits<Kind>::cells);
    const uint64_t states = space.count;
    const uint64_t world_bytes = meep::checked_product(tiles, sizeof(uint32_t));
    const uint64_t lut_bytes = states * sizeof(Entry);
    const uint64_t worlds_bytes = meep::checked_product(world_bytes, 2);
    if (lut_bytes > UINT64_MAX - worlds_bytes) throw std::invalid_argument("allocation size overflow");
    const uint64_t needed = worlds_bytes + lut_bytes;
    if (needed > SIZE_MAX) throw std::invalid_argument("allocation exceeds host address space");
    size_t free_bytes = 0, total_bytes = 0;
    CUDA_CHECK(cudaMemGetInfo(&free_bytes, &total_bytes));
    info.cuda_total_vram_before_alloc = total_bytes;
    info.cuda_free_vram_before_alloc = free_bytes;
    info.required_vram = needed;
    std::cerr << tv::shape_name(Kind) << " VRAM preflight: required " << needed << " bytes, free " << free_bytes
              << ", used/unavailable " << total_bytes - free_bytes << ", total " << total_bytes << '\n';
    if (needed > free_bytes)
        throw std::runtime_error("device memory required: " + std::to_string(needed) +
            " bytes (LUT " + std::to_string(lut_bytes) + "), free: " +
            std::to_string(free_bytes) + ". Reduce world/materials or select a smaller transition shape.");
    if (options.block_size > static_cast<unsigned>(info.properties.maxThreadsPerBlock) ||
        options.block_size > static_cast<unsigned>(info.properties.maxThreadsDim[0]))
        throw std::invalid_argument("block size exceeds device limit");
    if (meep::ceil_div(tiles, options.block_size) >
        uint64_t{static_cast<unsigned>(info.properties.maxGridSize[0])})
        throw std::invalid_argument("world exceeds the one-dimensional grid limit");

    std::cerr << "GPU: " << info.properties.name << " (sm_" << info.properties.major
              << info.properties.minor << "), " << info.properties.multiProcessorCount << " SMs, "
              << info.properties.maxThreadsPerMultiProcessor << " max threads/SM, L2 "
              << info.properties.l2CacheSize << " bytes\nWorld: " << options.x << 'x' << options.y << 'x'
              << options.z << ", " << tiles << " tiles, LUT " << lut_bytes << " bytes, buffers "
              << worlds_bytes << " bytes\nMode: " << options.launch << ", " << options.ticks
              << " timed ticks, " << options.repeats << " samples, verification "
              << (options.verify ? "enabled" : "disabled") << '\n';
    const auto world_space = meep::state_space(options.materials);
    std::vector<uint32_t> initial(tiles);
    for (uint64_t tile = 0; tile < tiles; ++tile)
        initial[tile] = meep::initial_state(tile, options.seed, world_space);
    DeviceBuffer<uint32_t> world_a(world_bytes), world_b(world_bytes);
    Stream stream;
    DeviceBuffer<Entry> table(lut_bytes);
    const Entry* lookup = table.data;
    const auto blocks = static_cast<unsigned>(std::min(uint64_t{65535}, meep::ceil_div(states, 256)));
    initialize_table<<<blocks, 256, 0, stream.value>>>(table.data, space);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaStreamSynchronize(stream.value));
    if constexpr (Kind == tv::Shape::cube) {
        benchmark_variant<Kind, false>(options, info, tiles, lut_bytes, initial,
            world_a, world_b, lookup, stream.value, output);
    } else {
        if (options.mode != "sequential")
            benchmark_variant<Kind, false>(options, info, tiles, lut_bytes, initial,
                world_a, world_b, lookup, stream.value, output);
        if (options.mode != "independent")
            benchmark_variant<Kind, true>(options, info, tiles, lut_bytes, initial,
                world_a, world_b, lookup, stream.value, output);
    }
}

void run(const Options& options) {
    DeviceInfo info;
    CUDA_CHECK(cudaSetDevice(static_cast<int>(options.device)));
    CUDA_CHECK(cudaGetDeviceProperties(&info.properties, static_cast<int>(options.device)));
    CUDA_CHECK(cudaDriverGetVersion(&info.driver_version));
    CUDA_CHECK(cudaRuntimeGetVersion(&info.runtime_version));
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
    // Only one LUT and one pair of world buffers are live at a time. Each
    // world/material/shape case records a fresh CUDA free/used memory query.
    if (options.variant == "all" || options.variant == "cube")
        run_shape<tv::Shape::cube>(options, info, *output);
    if (options.variant == "all" || options.variant == "face")
        run_shape<tv::Shape::face>(options, info, *output);
    if (options.variant == "all" || options.variant == "edge")
        run_shape<tv::Shape::edge>(options, info, *output);
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
