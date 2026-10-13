# CUDA neighborhood thread-count experiment

Measure how grid size affects a minimal 2x2x2 neighborhood update: one world
load, one rule-table load, and one output store. One tick updates every
neighborhood once, producing the complete next-state buffer. Compare a few
threads processing many tiles, a grid near resident capacity, and a grid with
one thread per tile. The default runs the complete dependent update:

```cpp
const uint32_t state = current[tile];
const uint32_t next_state = table[state];
next[tile] = next_state;
```

The table-load address depends on the current-state value, and the output
store's value depends on the table-load result. These dependencies exist within
each tile while the GPU can overlap work from other tiles and warps. Timing
covers this entire chain, and CPU verification checks the table's result in
the next buffer. Both scheduling kernels use this chain.

An optional buffer-only copy control (`--access copy`) reads each tile and
writes it unchanged without a table lookup. It is a separate comparison workload,
not the output-write stage of the primary update.

## Build and run

Requires a CUDA toolkit with `nvcc`, a compatible NVIDIA driver, and a C++17 host
compiler. The default build targets the RTX 2070's `sm_75` and includes PTX.

```sh
cd experiments/thread_count
make test                         # Portable host checks; no GPU/toolkit needed
make                              # Build only; does not start a benchmark
./build/thread_count --help
make run                          # 100x100x100, 8 materials, automatic sweep
```

Use `make CUDA_ARCH=86` for another architecture, or override `NVCC` and
`NVCCFLAGS`. Clean before changing build flags. `make run` replaces
`results/latest.csv`; result paths are relative to the current working directory.
Output directories are created automatically. Progress goes to stderr; use
`--output -` for CSV on stdout. Generated files are ignored by Git.

```sh
# A short GPU correctness run: partial blocks, tiny grids, odd total tick count.
./build/thread_count --world 10 14 18 --materials 2 \
  --workers 1,2,33,128,512 --block-sizes 32,128 \
  --warmup 2 --ticks 3 --repeats 2 --output results/smoke.csv

# Explicit sweep including the RTX 2070's advertised 2304 CUDA cores.
# That number is an experimental point, not its resident thread limit.
./build/thread_count --workers 1,32,256,2304,9216,18432,36864,73728,125000 \
  --block-sizes 128,256,512 --output results/2070.csv

# Automatic occupancy-based sweep with more samples.
./build/thread_count --block-sizes 128,256,512 --ticks 30 --repeats 7 \
  --output results/2070_auto.csv

# A larger working set; explicit workers skip the very slow serial points.
./build/thread_count --world 256 256 256 --workers 4096,16384,32768,65536,2097152 \
  --ticks 30 --repeats 7 --output results/large.csv

# Compare the default CUDA Graph sequence with ordinary host launches.
./build/thread_count --launch direct --output results/direct.csv

# Optional comparison with a buffer-only copy control.
./build/thread_count --access both --output results/with_copy_control.csv
```

Other options: `--materials N` (any integer from 2 through 16),
`--min-workers N` (lower bound for the automatic sweep),
`--access lookup|copy|both`, `--seed N`,
`--device N`, `--warmup N`, and `--no-verify`. Dimensions must be positive and even.
`--access` defaults to `lookup`, the complete dependent update.
Sixteen materials need a 16 GiB table and will fail the memory preflight on an
8 GiB GPU. Copy-only mode does not allocate a table. Serial lookup points can
take substantially longer than parallel points; increase timed ticks selectively.

For the five 8-material X×X×100 worlds, use `make world-sweep` or
`python3 sweep_worlds.py`. The runner estimates table + two buffers, queries
NVIDIA-SMI before each case, requires an additional 256 MiB of free headroom,
then runs the complete thread sweep. The benchmark also checks CUDA's available
memory immediately before allocation. Both checks and estimates are saved.
Use `--materials 10`, `12`, or `14` for the larger tables and `--min-workers 32`
to skip the very slow serial points. Results default to `results/world_sizes_N/`
for the chosen material count, so separate sweeps do not overwrite one another.

## Workload and scheduling

- A world contains `(X/2)*(Y/2)*(Z/2)` tiles: 125,000 for 100x100x100.
  Tiles are contiguous `uint32_t` codes, one per disjoint neighborhood. The
  eight material IDs are encoded densely in base `materials`: a code is
  `sum(voxel[i] * materials^i)` for `i = 0..7`. Eight materials use three bits
  per voxel; nonpower counts also use every index from zero to `materials^8-1`,
  without holes from nibble fields. Encoding is done before timing.
- The table has `materials^8` four-byte entries. Eight materials use a 64 MiB
  table; ten use 0.373 GiB, twelve 1.602 GiB, and fourteen 5.498 GiB. The two
  world buffers require an additional `8 * tiles` bytes combined.
- The table implements a deterministic synthetic permutation of state codes.
  It is deliberately unrelated to water/lava physics. Seeded initial states
  spread lookups across the table; bijective rules avoid many tiles collapsing
  into a common state. Real material distributions may have different locality.
  Nonpower counts cycle-walk the original permutation on the enclosing
  power-of-two domain until its output is valid; this preserves bijectivity and
  the original power-of-two rules. The initial world uses the same seeded hash,
  reduced modulo the state count. All of this work is outside kernel timing:
  the timed kernel reads the precomputed table directly, without encoding,
  hashing, cycle walking, or a modulo operation.
- `grid_stride` assigns tile `thread_id + k * launched_threads` to each worker.
  This preserves contiguous accesses among neighboring warp lanes. It models
  a static worker pool; no atomics or software task queue are involved.
- `one_per_tile` runs a separate kernel with one update per thread and no loop.
  The final block can contain idle threads. CUDA schedules excess blocks as
  capacity becomes available; software does not allocate a thread on demand.
- The automatic sweep includes powers of two, approximately 1/4, 1/2, 1, 2,
  4, and 8 times estimated resident capacity, and the complete tile count.
  Capacity comes from `cudaOccupancyMaxActiveBlocksPerMultiprocessor` for the
  actual kernel and block size. Requests are rounded up to whole blocks;
  requests smaller than a block launch that many threads in one smaller block.
  Equivalent launch configurations are deduplicated within each block-size sweep.
  Blocks are assigned to individual SMs: one block uses one SM, and a grid with
  fewer blocks than SMs leaves some SMs idle. Compare multiple block sizes to
  distinguish wider SM coverage from more resident warps on each SM.
- Every kernel reads the current buffer and writes the other buffer. Stream
  order completes one tick before the next tick uses its output. Pointers swap
  after every tick, including odd tick counts. Each sample starts from the same
  seed and runs the same warmup and timed tick sequence.

The partition stays fixed. This isolates scheduling and lookup cost; it is not
a complete Margolus simulator. Alternating shifted partitions would require
regrouping voxels or gathering/scattering them and would change memory traffic.
Rendering, realistic physics, and additional per-tile computation are excluded.

CUDA cores are arithmetic resources, not a count of independently resident
threads. Turing supports up to 32 resident warps per SM, with additional block
and register limits. The benchmark queries the device and kernel rather than
hardcoding a thread-pool size. See NVIDIA's
[Turing tuning guide](https://docs.nvidia.com/cuda/turing-tuning-guide/index.html),
[grid-stride loop explanation](https://developer.nvidia.com/blog/cuda-pro-tip-write-flexible-kernels-grid-stride-loops/),
and [occupancy API](https://docs.nvidia.com/cuda/cuda-runtime-api/cuda_runtime_api/group__CUDART__OCCUPANCY.html).

## Timing and interpretation

Initialization, allocation, table generation, host/device transfers, graph
construction/upload, warmup, and full correctness comparisons are outside the
CUDA event interval. The default graph encodes separate ordered kernels for
all timed ticks, reducing host submission gaps. `--launch direct` submits those
kernels from the host. Both measure the event interval divided by tick count,
including device dispatch and any submission gaps; this is sustained tick time,
not isolated instruction latency. Warmup resets for every sample. With zero
warmup, first-use effects can remain.

The CSV records hardware and configuration, all sample times, minimum/median/
p90/maximum time, tiles per second, and verification status/checksum. Its launch
columns distinguish requested workers, allocated threads, workers with tiles,
block count, registers, estimated resident capacity, and the occupancy limit.
`grid_to_capacity_ratio > 1` means more blocks are launched than the estimated
simultaneously resident capacity can hold. Occupancy is a resource-based limit,
not a measured achieved occupancy or a guarantee of evenly distributed blocks.

`logical_gb_per_second` counts 12 bytes per lookup tile or 8 bytes per copy tile.
It does **not** measure DRAM bandwidth: random lookups can fetch larger memory
sectors, and caches can serve some loads. A flat throughput curve alone cannot
prove DRAM saturation. Compare copy and lookup curves, several world sizes,
and different material counts. Use Nsight Compute's Memory Workload Analysis
to measure DRAM traffic and stalls before concluding bandwidth is the bottleneck.
The small default world has only 1,000,000 bytes across both world buffers, so
cache reuse and dispatch overhead can matter even with a 64 MiB lookup table.
Run multiple sweeps to check clock/temperature effects; samples run in ascending
grid order, and other GPU work can affect timings.

To plot the CSV, install `matplotlib` in your Python environment, then run:

```sh
python3 plot.py results/2070_auto.csv --output results/2070_auto.png
```

Plots default to the complete dependent update: current-state read, table lookup
using that state, then a write of the lookup result. They include sample min/max
shading and resident-capacity reference lines. Use `plot.py --access both` with
a CSV containing both modes for the optional three-panel control comparison.
Every curve measures a complete tick. The copy control omits the table lookup;
its time is not the output-write latency of the primary update. These are
separate runs, not separately timed stages to add together. Their occupancy
estimates can differ.

All thread-count graphs mark the hardware SP count with a black dashed line
and label the fastest measured median. On this RTX 2070 that count is 2,304;
the separate resident-capacity reference is 36,864 for the full 256-thread blocks.
Other architectures can specify `plot.py --hardware-cores N` explicitly.
Use `--compare-materials` on a combined timing CSV to create one world-comparison
panel per material count, optionally with its matching `--cache-summary` table.

## Verification

The default checks every output tile after every timed sample against a CPU
reference for the entire warmup + timed tick sequence. A mismatch exits nonzero.
Copy results must exactly equal the initial states. Checks happen after timing.
Portable host checks cover table sizes, overflow/invalid inputs, rule range and
small-table bijectivity, scheduling coverage, partial blocks, and buffer parity.

Inspection of the compiled `sm_75` instructions confirms the dependency chain
in both lookup kernels: a global load reads the current state into a register,
that register forms the table address, a second global load reads the result,
and the global store writes that result register. The original performance
baseline used these same lookup-kernel instructions.

On a CUDA system, run the smoke command above, then check memory access safety:

```sh
compute-sanitizer --tool memcheck --error-exitcode 1 ./build/thread_count \
  --world 10 14 18 --materials 2 --workers 1,33,128,512 \
  --warmup 1 --ticks 3 --repeats 1 --output results/memcheck.csv
```

## Measured world and material sweeps

The 2026-10-13 sweeps cover five worlds at 8, 10, 12, and 14 materials. The
new 10/12/14 sweep has 339 verified timing configurations (113 per material
count), with [timings](baselines/rtx2070_world_sizes_10_12_14_2026-10-13.csv),
[VRAM audits](baselines/rtx2070_world_sizes_10_12_14_memory_2026-10-13.csv),
[cache summary](baselines/rtx2070_world_sizes_10_12_14_cache_2026-10-13.csv), and
[raw counters](baselines/rtx2070_world_sizes_10_12_14_counters_2026-10-13/).
The earlier 138-configuration 8-material sweep remains available as
[timings](baselines/rtx2070_world_sizes_8_2026-10-13.csv),
[memory](baselines/rtx2070_world_sizes_8_memory_2026-10-13.csv),
[cache](baselines/rtx2070_world_sizes_8_cache_2026-10-13.csv), and
[raw counters](baselines/rtx2070_world_sizes_8_counters_2026-10-13/).

Hardware: RTX 2070, 36 SMs, 2,304 SPs, 8 GiB VRAM, 4 MiB L2. Build: CUDA 12.6.85,
`-O3`, C++17, `sm_75`; runtime 12.6, driver API 13.0, WSL. Timing configuration:
block size 256, 2 warmup ticks, 10 timed ticks, 3 samples, seed 42,
CUDA Graph launches. Each sample checks every output tile against the CPU
reference. The GPU drives a display; clocks and other GPU workloads were not
isolated. Times below are the lowest configuration medians, not lowest individual
samples. One tick updates every neighborhood. The new sweeps start at 32 workers;
the earlier 8-material sweep also includes 1–16 workers. The compiled instructions
in both timed lookup kernels are identical across these sweeps.

| World | Tiles | 8 materials ms/tick | 10 materials | 12 materials | 14 materials |
| --- | ---: | ---: | ---: | ---: | ---: |
| 100×100×100 | 125,000 | 0.0710 | 0.0764 | 0.0776 | 0.0772 |
| 200×200×100 | 500,000 | 0.2757 | 0.2970 | 0.2995 | 0.2985 |
| 300×300×100 | 1,125,000 | 0.6150 | 0.6609 | 0.6739 | 0.6689 |
| 400×400×100 | 2,000,000 | 1.0899 | 1.2526 | 1.2576 | 1.3048 |
| 500×500×100 | 3,125,000 | 1.7097 | 1.8217 | 1.9908 | 1.9972 |

For the new sweeps, 100³ winners use 32,768–73,728 threads; 200²×100 winners
use one thread per tile. Other winners vary between a resident-sized worker
grid and the full tile count. Most curves first reach within 5% of their best
median around 8,192–18,432 workers. The 400²×100 cases show larger sample
variation: the observed thresholds are 32,768, 262,144, and 9,216 workers for
10, 12, and 14 materials. The plots retain min/max shading. These noisy winner
and threshold differences do not establish a universal optimum.

The table size is `4 * materials^8` bytes. **Fourteen is the largest material
count whose full table fits in 8 GiB** with this representation; fifteen needs
9.55 GiB for the table alone. Actual feasibility also depends on current usage.

| Materials | Table bytes | Table GiB | Table + largest world's two buffers GiB |
| --- | ---: | ---: | ---: |
| 10 | 400,000,000 | 0.372529 | 0.395812 |
| 12 | 1,719,926,784 | 1.601807 | 1.625090 |
| 14 | 5,903,156,224 | 5.497743 | 5.521026 |
| 15 | 10,251,562,500 | 9.547512 | 9.570795 |

The two buffers grow from 1,000,000 to 25,000,000 bytes combined. Before each
new timing case, NVIDIA-SMI reported 1,427 MiB used and 6,580 MiB free; the
largest allocation was 5,653.53 MiB, plus the runner's required 256 MiB headroom.
CUDA reported more free memory on this WSL setup; the audits save both interfaces,
and the runner gates on NVIDIA-SMI as well as CUDA's allocation check. The earlier
8-material timing sweep had 2,308–2,330 MiB free. Memory estimates and live checks
are required for subsequent GPU experiments.

| Materials | World | Best ms/tick | Threads | L1 sector hits | L2 sector hits |
| --- | --- | ---: | ---: | ---: | ---: |
| 10 | 100×100×100 | 0.0764 | 32,768 | 0.002240% | 6.48% |
| 10 | 200×200×100 | 0.2970 | 500,224 | 0.002528% | 5.85% |
| 10 | 300×300×100 | 0.6609 | 1,048,576 | 0.002731% | 5.84% |
| 10 | 400×400×100 | 1.2526 | 32,768 | 0.002608% | 5.81% |
| 10 | 500×500×100 | 1.8217 | 3,125,248 | 0.002527% | 5.80% |
| 12 | 100×100×100 | 0.0776 | 73,728 | 0.000704% | 6.31% |
| 12 | 200×200×100 | 0.2995 | 500,224 | 0.000576% | 5.79% |
| 12 | 300×300×100 | 0.6739 | 36,864 | 0.000704% | 5.67% |
| 12 | 400×400×100 | 1.2576 | 262,144 | 0.000580% | 5.62% |
| 12 | 500×500×100 | 1.9908 | 3,125,248 | 0.000632% | 5.59% |
| 14 | 100×100×100 | 0.0772 | 65,536 | 0.000128% | 6.27% |
| 14 | 200×200×100 | 0.2985 | 500,224 | 0.000128% | 5.64% |
| 14 | 300×300×100 | 0.6689 | 1,048,576 | 0.000100% | 5.59% |
| 14 | 400×400×100 | 1.3048 | 2,000,128 | 0.000180% | 5.59% |
| 14 | 500×500×100 | 1.9972 | 3,125,248 | 0.000138% | 6.30% |

GPU verification passed for all 339 new timing records. Host checks exhaustively
validate nonpower permutation domains at 3 and 5 materials, sample the requested
large domains, and check scheduling/parity. Compute Sanitizer also reported zero
errors on a 3-material partial-block workload with odd total tick count.

## Cache measurements and data placement

Both packed world buffers and the rule table are allocated in global GPU memory
with `cudaMalloc`. Registers hold indices and loaded state/result values. No
explicit shared memory is used; compiled resource usage and profiled static/
dynamic shared allocations are all zero. The hardware automatically caches
global accesses through L1/TEX per SM and the shared 4 MiB L2. The profiled launches
reserve 32 KiB of the unified hardware pool for shared memory, leaving 64 KiB for
L1/TEX per SM on Turing; that reservation does not mean the kernel uses shared
memory. See NVIDIA's [Turing cache configuration](https://docs.nvidia.com/cuda/turing-tuning-guide/index.html#unified-shared-memory-l1-texture-cache).

Cache rates come from separate Nsight Compute 2024.3.1 runs at each world's
winning thread count and scheduling kernel. Ten ticks are profiled after two
warmups. Application replay repeats setup/warmup for each of three collection
passes; `--cache-control none` prevents the profiler's usual cache flush, and
`--clock-control none` leaves clocks unchanged. The profiler's instrumented
event times are excluded from the timing table. See the
[Nsight Compute profiling guide](https://docs.nvidia.com/nsight-compute/ProfilingGuide/).

Hit rates are `sum(hit sectors) / sum(hit + miss sectors)` across those ten ticks,
using `l1tex__t_sectors_lookup_{hit,miss}.sum` and
`lts__t_sectors_lookup_{hit,miss}.sum`. They aggregate the kernel's buffers and
lookup table, including writes; they are not isolated rule-table hit rates.
The raw records also preserve DRAM reads/writes and the matched kernel/thread
counts. The random-state workload has little cache reuse; good throughput is
consistent with many independent memory requests hiding latency. These counters
alone do not prove DRAM bandwidth saturation. Real material distributions can
reuse a much smaller subset of rule-table entries.

Reproduce the sweep, cache profiles, and graph/table with a normally installed
`nvcc`, `nvidia-smi`, `ncu`, and Python `matplotlib`:

```sh
make
python3 sweep_worlds.py
python3 profile_cache.py results/world_sizes_8/measurements.csv
python3 plot.py results/world_sizes_8/measurements.csv --compare-worlds \
  --cache-summary results/world_sizes_8/cache/cache_summary.csv \
  --output results/world_sizes_8/tick_times_and_cache.png

# New material sweeps; fresh VRAM checks happen before every world/profile.
for materials in 10 12 14; do
  python3 sweep_worlds.py --materials "$materials" --min-workers 32
  python3 profile_cache.py "results/world_sizes_${materials}/measurements.csv"
  python3 plot.py "results/world_sizes_${materials}/measurements.csv" --compare-worlds \
    --cache-summary "results/world_sizes_${materials}/cache/cache_summary.csv" \
    --output "results/world_sizes_${materials}/tick_times_and_cache.png"
done

# Render the curated combined 10/12/14 measurements.
python3 plot.py baselines/rtx2070_world_sizes_10_12_14_2026-10-13.csv --compare-materials \
  --cache-summary baselines/rtx2070_world_sizes_10_12_14_cache_2026-10-13.csv \
  --output results/world_sizes_10_12_14/tick_times_and_cache.png
```

On WSL, Windows must permit GPU counter access. If profiling reports
`ERR_NVGPUCTRPERM`, use the Windows NVIDIA App/Control Panel counter settings;
see [NVIDIA's instructions](https://developer.nvidia.com/nvidia-development-tools-solutions-err_nvgpuctrperm-permission-issue-performance-counters).
The current session enabled that setting. Its profiler is available temporarily
at `/tmp/meep-nsightcompute2024.3.1/target/linux-x64/ncu`; pass that via `--ncu` and
`--section-folder /tmp/meep-nsightcompute2024.3.1/sections` when reproducing here.

The [earlier 100³/control baseline](baselines/rtx2070_2026-10-13.csv) remains
available. Portable host checks, partial-block/odd-even GPU checks, and direct
launch checks pass. Compute Sanitizer memcheck reported zero errors on the small
verification workload; sanitizer timings are excluded from performance results.

Verification used a temporary toolkit at `/tmp/meep-cuda-12.6`; while it remains
available, rebuild here with `make NVCC=/tmp/meep-cuda-12.6/bin/nvcc`. The project
does not require this path on machines with a normally installed CUDA toolkit.
