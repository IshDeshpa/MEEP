Synthetic 1 / 6 / 12 lookup benchmark

WHAT IT TESTS
  Full cube: 16^8 entries * uint32_t = 16 GiB; one lookup per cube.
  Six faces: 16^4 entries * uint16_t = 128 KiB; six lookups per cube.
  Twelve edges: 16^2 entries * uint8_t = 256 bytes; twelve lookups per cube.
  All tables use cudaMalloc device GLOBAL memory. Small tables may hit caches.
  There is no shared-memory staging or constant-memory table in this benchmark.
  Tables are initialized directly on the GPU, outside timing; no giant CPU table.

Two execution modes:
  independent_proposals: all subtiles read the original cube; results are folded
    into a checksum. No conflict resolution or next-state assembly is performed.
  sequential_overwrite: each subtile reads the state left by the preceding one
    and overwrites its own cell nibbles; later lookups depend on previous results.
  Cube mode performs one lookup and is only run once (mode=single).

These are SYNTHETIC FSM transitions (a deterministic integer hash, truncated to
output width), not the project's actual reaction rules. Results across the three
neighborhoods/modes need not match. PASS means agreement with a CPU reference
for that particular variant/mode. This experiment cannot establish that the
three algorithms implement an equivalent automaton. It isolates realistic LUT
sizes/counts, index extraction costs, and the penalty of sequential dependencies.
Sequential mode differs in both dependencies and work/semantics; the timing
change is not a pure measurement of memory latency. There is no claiming scheme.

Cube cells are numbered x+2*y+4*z; each occupies four bits of a uint32_t.
Faces are x=0,x=1,y=0,y=1,z=0,z=1. Edges are the four x edges, four y edges,
then four z edges. Faces/edges overlap; they are not disjoint subdivisions.
Inputs are deterministic pseudo-random cube states, not spatially correlated
lattice states. Real distributions may access a much smaller hot table subset.

BUILD on an NVIDIA GPU/CUDA machine:
  make
For A100:
  make NVCCFLAGS='-O3 -std=c++17 -lineinfo -Xptxas=-v -arch=sm_80'
For H100 (CUDA toolkit supporting sm_90):
  make clean
  make NVCCFLAGS='-O3 -std=c++17 -lineinfo -Xptxas=-v -arch=sm_90'
For your RTX 4060 Laptop:
  use -arch=sm_89. The full cube table DOES NOT fit in its ~8-GiB memory.

RUN:
  ./fsm_lookup_bench all 1000 512 5 both
Arguments:
  VARIANT ITERATIONS BLOCKS REPEATS MODE
  VARIANT: all, cube, faces, edges
  ITERATIONS: cube inputs processed per thread (default 1000)
  BLOCKS: thread blocks (default 512); fixed 256 threads/block
  REPEATS: timed repetitions after warmup (default 5)
  MODE: both, independent, sequential (default both)
Small functional tests:
  ./fsm_lookup_bench edges 10 2 1
  ./fsm_lookup_bench faces 10 2 1
Full cube requires 16 GiB even when processing only a few inputs:
  ./fsm_lookup_bench cube 10 2 1

On Lonestar6, put the files in one directory and submit:
  sbatch fsm_lookup.slurm
or:
  sbatch -p gpu-h100 fsm_lookup.slurm
Script preserves your allocation TRA25006, uses CUDA 12.8, and compiles for both
A100 and H100. Lonestar6 uses GPU partitions without --gres. No job submitted
by Codex. Script syntax checked, execution on TACC remains outstanding.

MEMORY SAFETY / SKIPS
  One table allocated at a time. Allocation size uses 64-bit arithmetic.
  If a requested table cannot fit with 64-MiB headroom, it prints SKIP on stderr
  and returns exit code 2 after completing the other requested variants.
  Any CUDA failure/checksum mismatch returns 1. Complete success returns 0.
  A SKIP is not a full-table benchmark result. Do not substitute a small table
  and claim you tested the 16-GiB working set.

OUTPUT / INTERPRETATION
  GPU/model/SM count and launch configuration, then CSV:
  variant,mode,lookups_per_cube,entry_bytes,table_bytes,median_ms,
  Mcubes_per_s,aggregate_ns_per_cube,validation
  Each row performs BLOCKS*256*ITERATIONS cube-processing operations.
  Compare Mcubes_per_s or aggregate_ns_per_cube, not raw lookups/s.
  aggregate_ns_per_cube is reciprocal aggregate throughput, NOT per-thread latency.
  Logical LUT bytes per cube: cube=4, faces=12, edges=12. Actual transferred bytes
  depend on cache hits and transactions and can differ greatly.
  Copies/initialization/reference checking excluded from CUDA-event timings.
  Every thread writes a checksum; first/last warp (64 lanes) checked on CPU.
  Volatile LUT loads deliberately preserve accesses; they are not synchronization
  and are not a blanket recommendation for your production kernels.

GPU CHECKS / PROFILING (tools installed and permitted):
  compute-sanitizer --tool memcheck ./fsm_lookup_bench edges 10 2 1
  compute-sanitizer --tool memcheck ./fsm_lookup_bench faces 10 2 1
  ncu --section MemoryWorkloadAnalysis --section LaunchStats \
      --launch-skip 2 --launch-count 1 ./fsm_lookup_bench faces 1000 512 1 independent
The ncu command skips table initialization and warmup, then profiles the timed
kernel. Inspect hit rates, memory sectors, memory stalls, registers and occupancy.
Use ordinary runs for timing; profiling/replay perturbs cache and execution.

VALIDATION STATUS
  CPU checks passed for the six face masks, twelve unique adjacent edges, and
  extraction/replacement across 1000 generated cube states.
  No nvcc or NVIDIA GPU was available in the authoring environment: CUDA build,
  GPU checksum checks, memory allocation, sanitizer and timing are UNVERIFIED.
  Run small tests before larger measurements. GPU test does not need new laptop
  packages if compiled/run on TACC.
