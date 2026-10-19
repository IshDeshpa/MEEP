# Transition variations

Combine the `thread_count` experiment's one-thread-per-tile kernel and ping-pong
world buffers with the `fsm_lookup_test` experiment's cube, face, and edge
transitions. Every active thread reads a complete 2x2x2 tile from `current`,
performs its LUT operations in a temporary tile, and writes one complete tile
to `next`. Every run uses one thread per tile. The experimental axes are
transition shape, sequential/independent mode, world dimensions, and material
count; there is no worker-count sweep.

| Variant | LUT key | Lookups per tile | Entry type | Modes |
| --- | --- | ---: | --- | --- |
| `cube` | 2x2x2, eight cells | 1 | `uint32_t` | `single` |
| `face` | 2x2x1, four cells | 6 | `uint16_t` | `independent`, `sequential` |
| `edge` | 2x1x1, two cells | 12 | `uint8_t` | `independent`, `sequential` |

Cube retains the original dependent update exactly:

```cpp
state = current[tile];
result = table[state];
next[tile] = result;
```

For faces and edges, the update is:

```cpp
original = decode(current[tile]);
temporary = original;
for (part : ordered_faces_or_edges) {
    source = sequential ? temporary : original;
    key = extract(source, part);
    result = table[key];
    overwrite(temporary, part, result);
}
next[tile] = encode(temporary);
```

Independent mode always derives keys from the original tile. Sequential mode
derives each key from the temporary tile after earlier overwrites. In both modes,
later parts win when their proposed voxels overlap. This produces actual next
tiles, unlike the earlier independent FSM benchmark's accumulated checksums.
The five variants implement different synthetic automata; matching their outputs
to each other is not a correctness requirement.

## Build and run

```sh
cd experiments/transition_variations
make test                         # Portable host checks, no CUDA required
make                              # Build only; nvcc, default sm_75 plus PTX
./build/transition_variations --help
make run                          # All five variants, one thread per tile

# Small GPU verification, including partial blocks and odd total tick count.
./build/transition_variations --world 10 14 18 --materials 3 \
  --warmup 2 --ticks 3 --repeats 2 --output results/smoke.csv

# Compare all five variants at another world size and material count.
./build/transition_variations --world 200 200 100 --materials 14 \
  --output results/materials_14.csv

# Select a smaller table and ordinary host launches.
./build/transition_variations --variant face --mode independent \
  --materials 16 --launch direct --output results/face_independent.csv
```

Use `make CUDA_ARCH=86` for another GPU architecture; clean before changing
build flags. `NVCC`, `NVCCFLAGS`, `CXX`, and `CXXFLAGS` can be overridden.
`make run` replaces `results/latest.csv`. Output directories are created
automatically, and build/results files are ignored by Git. CSV goes to the
selected path, or stdout with `--output -`; progress goes to stderr.

`--variant cube|face|edge|all` defaults to `all`.
`--mode independent|sequential|both` defaults to `both` and only filters face/edge
modes; cube always runs once. Other options follow `thread_count`:
`--world X Y Z`, `--materials N`, `--block-size N`,
`--launch graph|direct`, `--warmup N`, `--ticks N`, `--repeats N`,
`--preheat-ms N`, `--seed N`, `--device N`, and `--no-verify`. See `--help` for defaults.
The block size defaults to 256 and remains fixed throughout each run.

### World/material comparison sweep

`sweep.py` defaults to X×200×X for X = 500, 600, 700, 800, 900, 1000,
with 8, 10, 12, and 14 materials and all five variants: **120 configurations**.
The tile count is `(X/2) * (200/2) * (X/2)`, from 6,250,000 to 25,000,000.
The two world buffers grow from 50,000,000 to 200,000,000 bytes combined.
Changing world size changes only buffer size and update count, not tile
geometry or LUT size. Changing material count changes the LUT and encoded
state domain. The partition remains fixed.

```sh
python3 sweep.py --output-dir results/world_material_sweep --resume
python3 profile_counters.py results/world_material_sweep/measurements.csv \
  --ticks 5 --skip-profile-verify --resume \
  --output-dir results/world_material_sweep/counters
# Requires Python matplotlib; NumPy is installed with it.
python3 analyze.py results/world_material_sweep/counters/report.csv \
  --output-dir results/world_material_sweep/analysis
```

Open `analysis/report.html` for the self-contained interactive comparison.
It embeds its data and plots and works offline. Select world size or material
count as the horizontal axis, hold the other fixed, and compare update methods
using runtime, instructions, cache behavior, DRAM traffic, occupancy, or warp
states. The same report includes static plots, matched speedup/material-ratio
matrices, and a downloadable normalized CSV. PNG/SVG/PDF plots are also saved
beside it. Raw counter CSVs and `.ncu-rep` files remain under `counters/`.

The sweep uses five samples of ten timed ticks after two warmup ticks, block
size 256, seed 42, CUDA graphs, and 500 ms of untimed GPU preheating before
**each** sample. It then resets the world before the logical warmup/timed
sequence. This avoids the multi-fold clock-ramp effects observed when short
samples follow an idle CPU-reference interval; preheating is excluded from
tick time and does not change the verified state trajectory. It does not
lock GPU clocks. Standalone native runs expose `--preheat-ms` (default 0);
`make run` and `sweep.py` request 500 ms explicitly.

Eight CPU workers calculate the independent deterministic tile trajectories
outside GPU timing, without extra world copies. Every tile after every native
sample is still compared with the CPU result. `--skip-profile-verify` avoids
repeating that expensive reference during each application-replay pass: the
input timing rows must already be fully verified, instrumented timing CSVs
are marked unverified, and the joined report records `verified_native_source`.
Native verification is never skipped by `sweep.py`.

Cases run in a reproducible shuffled order. Before each world/material case,
the runner records estimates for all LUTs and both world buffers and checks
current NVIDIA-SMI free/used memory with 256 MiB headroom. CUDA checks memory
again before each shape allocation. `memory_checks.csv`, per-case timing/log/
command files, the combined `measurements.csv`, and `run_context.json` save
this context, including a binary SHA-256. `--resume` reuses matching completed
cases; changed binaries require a new output directory. Profiler `--resume`
reuses completed matching profiles.

The measured RTX 2070 sweep is available locally as an
[interactive report](results/xz500_1000_y200_m8_14_warm_2026-10-18/analysis/report.html)
and [comparison CSV](results/xz500_1000_y200_m8_14_warm_2026-10-18/analysis/comparison_data.csv).
All 120 timing configurations pass full CPU/GPU verification and have all 19
requested hardware metrics. Sequential edge has the fastest measured median in
23 of 24 world/material cases; at 1000×200×1000 it takes 1.546–1.574 ms/tick versus
13.897–15.201 ms for cube. At 600×200×600 with 8 materials, face sequential is
slightly faster (0.697 versus 0.720 ms), with overlapping sample ranges.
Quadrupling tile count increases tick time by
3.65–4.07×. Material effects differ by update method: at the largest world,
8→14 materials changes edge sequential runtime by 1.00×, face sequential by
1.80×, and edge independent by 3.24×. The report includes instructions, cache
traffic/hit rates, DRAM bytes, and scheduler comparisons with their measurement
context. Generated artifacts stay under
`results/xz500_1000_y200_m8_14_warm_2026-10-18/`; the earlier unheated diagnostic
run is excluded from these comparisons.

## Representation and ordered parts

Both world buffers contain contiguous `uint32_t` tiles. Cell numbering is
`cell = x + 2*y + 4*z`. A tile is the dense base-`materials` code
`sum(voxel[cell] * materials^cell)`. A face/edge key uses the same encoding in
the part's cell order. All integer material counts from 2 through 16 are valid,
without unused nibble indices for nonpowers of two. Face/edge extraction,
decoding LUT results, overwriting the temporary cells, and encoding the output
are included in the timed kernel.

Faces run in this order: `x=0`, `x=1`, `y=0`, `y=1`, `z=0`, `z=1`.
Edges run along x first, then y, then z; their endpoint indices are:
`01, 23, 45, 67; 02, 13, 46, 57; 04, 15, 26, 37`.
The geometry helpers come from `fsm_lookup_test/fsm_helpers.h`.

Each voxel belongs to three faces and three edges. Under independent fixed-order
overwrite, the last two faces or last four edges determine the final voxel
values. **All six/twelve table loads still execute.** Independent kernels use
volatile LUT loads to preserve accesses whose proposals are overwritten;
ordinary loads would let the compiler discard those lookups. Volatile access
can also affect caching and optimization, so timing differences include that
effect. It provides no inter-thread synchronization. Cube/sequential loads
remain ordinary loads because their data dependencies already preserve them.

One shared LUT is allocated per shape, not one per face/edge orientation.
It contains every combination of the part's material IDs:

| Materials | Cube bytes (`4*N^8`) | Face bytes (`2*N^4`) | Edge bytes (`N^2`) |
| ---: | ---: | ---: | ---: |
| 8 | 67,108,864 | 8,192 | 64 |
| 10 | 400,000,000 | 20,000 | 100 |
| 12 | 1,719,926,784 | 41,472 | 144 |
| 14 | 5,903,156,224 | 76,832 | 196 |
| 16 | 17,179,869,184 | 131,072 | 256 |

The LUT uses `thread_count`'s deterministic synthetic permutation, cycle-walked
into the part's state domain, and is initialized on the GPU outside timing.
These are scheduling/lookup workloads, not material reaction rules. The seeded
initial world is identical across variants. Every table result contains valid
material IDs. No giant host LUT is allocated.

Before allocating each shape's LUT and both worlds, the benchmark estimates
their combined bytes and queries CUDA's current free/used memory. Those values
are saved with every measurement. Only one LUT and world-buffer pair is live
at a time. Insufficient free memory is an error; select `face` or `edge` when
the cube table does not fit. GPU capacity alone does not establish availability.

## Scheduling, timing, and validation

Each kernel has one tile update per active thread, with idle lanes in a partial
final block. There is no grid-stride loop. Blocks use the configured size, and
the launched grid covers the full tile count, corresponding to the rightmost
thread-count point. Occupancy queries use each actual variant/mode/kernel and
block size. The CSV distinguishes launched/working threads, SM count,
register/local/shared storage, resident thread capacity, and warp occupancy
limit. CUDA core count is not used as a resident-thread limit.

Each output tile has exactly one writer. Ordered kernels in one stream finish
each tick before buffers change roles; both warmup and timed ticks swap buffers.
The partition is fixed: there is no shifted Margolus partition, cross-tile
interaction, or boundary exchange. This isolates the transition and scheduling
cost and is not a complete Margolus simulation.

CUDA events time complete tick sequences (graphs by default), divided by timed
tick count. Allocation, initialization, transfers, graph construction/upload,
warmup, and verification are outside timing. Each sample resets the initial
world and checks every final tile against a separate CPU reference for that
variant after all warmup + timed ticks. A mismatch exits nonzero.

CSV records per-sample times, minimum/median/p90/maximum, whole-world ticks per
second, tiles per second,
verification/checksum, and configuration/memory context. Logical bandwidth
counts 12 bytes per cube tile and 20 bytes per face/edge tile: one four-byte
world load, all LUT entry loads, and one four-byte world store. This is not
measured DRAM traffic. Hardware counters come from the separate profiling
workflow below.

## Timing and counter reports

```sh
make run                              # Uninstrumented timings: results/latest.csv
make profile                          # Requires ncu + access to GPU performance counters
# Joined report: results/counters/report.csv

# Profile one or several timing CSVs, preserving every variant/mode row.
python3 profile_counters.py results/latest.csv
python3 profile_counters.py results/m*.csv --output-dir results/material_counters
```

Use **`median_ms_per_tick`** for whole-world step time, with the sample range
and `ticks_per_second`. Every measured tick calculates all tiles and writes
the entire next buffer. Counter collection does not replace these original
uninstrumented times.
The `timing_context` column labels normal versus instrumented runs. The profiler
runner accepts only verified `uninstrumented` timing rows and labels its own
benchmark output `instrumented`. Use `--instrumented` for manual profiler or
sanitizer launches as well.

The joined `report.csv` retains all timing/configuration columns and adds:

| Counter group | Report fields |
| --- | --- |
| L1/TEX and L2 | Hit/miss sectors per tick and sector hit percentages |
| DRAM | Read/write bytes per tick and percent of peak sustained throughput |
| SM | Throughput and active-cycle percentages |
| Occupancy and scheduling | Achieved occupancy, issue-active percentage, active/eligible warps per scheduler |
| Instructions | Warp instructions executed per tick |
| Warp stalls | Long/short scoreboard, math-pipe throttle, and not-selected percentages |
| Instrumented diagnostic | `profiled_mean_kernel_ms`, kept separate from normal tick timing |

The script queries supported metrics before collecting the 19 requested
hardware metrics. Unsupported, missing, or nonnumeric counters stay blank and
appear in `unavailable_metrics`; `profile_status` is `measured`, `partial`, or
`unavailable`. A failed profiling case preserves its normal timing in the
report and adds the failure reason. Exit code 2 indicates a failed case.

Counter runs reproduce each row's world, material count, variant/mode, seed,
block size, preheating, and device. They use one thread per tile with ordinary direct
launches, one repeat, and the timing row's warmup/timed tick counts. Override
those counts with `--warmup` and `--ticks` when needed. Kernel filters exclude
LUT initialization and preheat kernels; launch-skip excludes warmup. The report records actual
kernel names, launched threads, profiler version, and replay/cache/clock settings.

Application replay repeats deterministic setup and warmup for each counter
collection pass. `--cache-control none` preserves natural cache state, and
`--clock-control none` leaves clocks alone. This follows
[NVIDIA's application-replay guidance](https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html#application-replay).
All profiling event times are instrumented diagnostics. Percentages/ratios
are arithmetic means across profiled ticks; cache hit percentages use summed
hit/miss sectors. Zero observed cache sectors leave the hit percentage blank.
Cache counters aggregate the world buffers and LUT, including writes; they
are not isolated LUT hit rates. Stall counters describe warp states and do
not by themselves establish a performance bottleneck.

Before every profiled case, the script recomputes LUT + two-buffer bytes and
checks NVIDIA-SMI's current free/used memory, with 256 MiB headroom by default
(`--headroom-mib`). CUDA checks memory again inside every application replay.
Both interfaces' memory checks are saved. Each case also preserves raw counter
CSV, an `.ncu-rep` report, the exact command as JSON, a log, and a separate
instrumented timing CSV under the chosen output directory.

Override `--ncu` and `--section-folder` for a nonstandard installation, or pass
those through `make profile PROFILE_FLAGS='...'`. The validation environment's
temporary installation can be used with:

```sh
python3 profile_counters.py results/latest.csv \
  --ncu /tmp/meep-nsightcompute2024.3.1/target/linux-x64/ncu \
  --section-folder /tmp/meep-nsightcompute2024.3.1/sections
```

`make test` checks small-domain permutations, larger material domains, exact
lookup inputs/counts, overwrite precedence, partial blocks,
one writer per tile, and warmup/timed buffer parity for all five variants.
It also checks counter parsing, weighted cache rates, missing-counter handling,
and profiling command configuration.
CUDA 12.6 / `sm_75` validation on the RTX 2070 passed all five variants' full
CPU/GPU comparisons, graph/direct launches, and odd/even tick sequences.
Compute Sanitizer reported zero errors. Machine-code inspection confirmed one
world load, all 1/6/12 LUT loads, and one world store per active thread. Nsight
Compute 2024.3.1 collected all 19 requested metrics for all five configurations;
the joined report preserved the uninstrumented timing values exactly.
On a CUDA system, run the smoke command above and then:

```sh
compute-sanitizer --tool memcheck --error-exitcode 1 ./build/transition_variations \
  --world 10 14 18 --materials 3 \
  --warmup 1 --ticks 3 --repeats 1 --instrumented --output results/memcheck.csv
```
