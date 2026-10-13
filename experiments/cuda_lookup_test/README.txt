CUDA byte lookup / shared-memory bank experiment

Purpose: learn memory-access behavior, not implement a cellular automaton.
One entry is one byte. The default table has 256 entries: the 16^2 pair-state
lookup size. Input/output grid traffic and reaction merging are not modeled.

BUILD on a machine with CUDA Toolkit and an NVIDIA GPU:
  make
or:
  nvcc -O3 -std=c++17 -lineinfo -Xptxas=-v lookup_memory.cu -o lookup_memory
Do not add -G for performance measurements.
If the CUDA installation requires an architecture flag, add the appropriate
-arch=sm_XX for your GPU via NVCCFLAGS. No GPU model is assumed by this bundle.

RUN:
  ./lookup_memory
Arguments (all optional, in this order):
  TABLE_BYTES ITERATIONS BLOCKS REPEATS SPACE PATTERN
Defaults: 256 10000 128 5 -1 -1. -1 selects all spaces/patterns.
The thread block is fixed at 256 threads. TABLE_BYTES must be power-of-two.

Space codes:
  0 global_byte: cudaMalloc table, byte indexing
  1 shared_byte: per-block shared table, byte indexing
  2 shared_packed: same shared contents, explicit 32-bit load + byte extraction
Pattern codes:
  0 same_byte: all lanes read the same byte
  1 same_word_4bytes: lanes choose the four bytes in one aligned word
  2 consecutive_bytes: lanes choose consecutive byte entries
  3 two_words_same_bank: lanes alternate two words 128 bytes apart
  4 pseudo_random: deterministic scattered indices

The shared table is always 256 bytes, loaded cooperatively once per block.
With the conventional 32-bank / 4-byte-word organization, pattern 3 deliberately
hits two different words in one bank; patterns 0 and 1 share a single word.
These are hypotheses to verify on your GPU. Byte-load and word-load instructions
may differ, so do not assume all timings or transaction counts are identical.

Examples:
  ./lookup_memory 256 10000 128 5
  ./lookup_memory 256 10000 128 5 1 3
  ./lookup_memory 256 10000 128 5 2 1
Global-table working-set sweep (only random global loads):
  for bytes in 256 65536 1048576 16777216 67108864; do
      ./lookup_memory "$bytes" 10000 128 5 0 4
  done
The 256-byte global table will likely hit cache; cudaMalloc does NOT force each
lookup to reach DRAM. Larger-table tests may exceed caches, depending on GPU.
Patterns 0..3 deliberately use restricted index patterns; random is the better
starting point for exploring cache capacity. Shared cases always use 256 bytes,
so do not compare their footprint directly to a larger global table as if only
memory placement changed.

OUTPUT:
  GPU identity, compute capability, SM count, L2 size, device-memory size,
  CUDA versions, launch dimensions, and CSV rows for each selected case.
  median_ms: CUDA-event elapsed time, median of repetitions after a warmup.
  Glookups_per_second: aggregate logical lookup throughput, NOT per-load latency
  and NOT physical DRAM bandwidth. Packed mode performs 32-bit loads for byte
  results. Kernels include index arithmetic and checksum accumulation.
  PASS: first and last warp checksums match the CPU reference (64 lanes).

TIMING DETAILS:
  Host allocation, table copy, result copy and CPU checks are excluded.
  Shared cases include the per-block table initialization and barrier.
  Loads are explicitly volatile to stop redundant-load elimination. This is a
  synthetic memory-access test; volatile is not a recommendation for your real
  lookup kernel and may change cache/compiler behavior.
  Inputs are independent of preceding lookup values, although the accumulator
  itself creates a dependence. This measures throughput, not pointer-chase latency.

PROFILE on the GPU machine (if installed and performance counters permitted):
  compute-sanitizer --tool memcheck ./lookup_memory 256 100 2 1
  compute-sanitizer --tool racecheck ./lookup_memory 256 100 2 1 1
  ncu --section MemoryWorkloadAnalysis --section LaunchStats \
      --launch-skip 1 --launch-count 1 ./lookup_memory 256 10000 128 1 2 3
The ncu command skips the warmup and profiles one timed shared-packed bank-conflict
kernel. Change the final SPACE/PATTERN to compare other cases. Profiling can
perturb timing; use ordinary runs for timing and ncu to explain counters.
Compare shared-memory bank conflicts, global sectors/requests, cache hit rates,
register use and occupancy. Metric availability varies with GPU/tool version.
CPU perf counters do not substitute for GPU profiling.

VALIDATION STATUS:
  CPU helper tests passed: packing/unpacking all 256 entries, same-word grouping,
  and deliberate same-bank/different-word mapping. This environment had neither
  nvcc nor an NVIDIA GPU, so CUDA compilation, GPU execution and sanitizer checks
  have NOT been performed. Run the small sanitizer cases before measurements.
