# MEEP

A group final project for a multithreaded programming class: a 3D voxel material
world inspired by Powder Game.

The current candidate design updates disjoint 2x2x2 Margolus neighborhoods using
a rule-table lookup and alternates current/next world buffers. Scheduling,
representation, material rules, and rendering are still being explored.

## Repository map

| Location | Purpose |
| --- | --- |
| [Project guidance](AGENTS.md) | Current conventions and navigation for contributors and coding agents |
| [Architecture working model](docs/architecture.md) | Alpha scope, CPU/GPU ownership, alternatives, and open questions |
| [CUDA thread-count experiment](experiments/thread_count/README.md) | Compare small grids, occupancy-sized worker grids, and one thread per neighborhood |
| [Transition variations](experiments/transition_variations/README.md) | Compare cube, face, and edge LUT updates with one thread per tile and double buffering |
| [CPU update proof of concept](experiments/sim_update_poc/README.md) | Serial updates, per-cell threads, and persistent worker pools |

## World state prototype

The main prototype in `src/` keeps visible dimensions `n,m,k` and allocates
`(n+2)*(m+2)*(k+2)` cells with an unrendered, one-cell BLOCK shell. Both device
buffers start with AIR inside and BLOCK outside. Tile access uses visible-world
coordinates through DeviceWorldView; padding and strides are handled internally.
The renderer's signed coordinate access also supports the padded halo.
Face types, packing helpers, binary LUT loading, and device lookup live in
`src/state/lut.hpp/cu`. World ticks use the LUT; `worldTick` waits for completion
and calls `swapBuffers`. Scene loading, rendering integration, and starting the
ticker from main are still being completed. See the [architecture model](docs/architecture.md).

`make -C src run-world_blank` loads `src/state/la.bin` and checks seven GPU ticks
on four blank worlds, including 100x100x100. It verifies AIR interiors, BLOCK
padding, state bits, output coverage, LUT upload, and buffer parity using
`updateWorld`, `waitForTick`, and `swapBuffers`. Supply another LUT with
`ARGS=/absolute/path/to/rules.bin`. Set `CUDA_HOME` for a nonstandard toolkit.
The older `world_padding` test still needs its call sites adapted to the LUT API.

## CUDA experiment

```sh
make -C experiments/thread_count test  # Host checks; no CUDA needed
make -C experiments/thread_count      # Requires nvcc; targets RTX 2070 / sm_75
make -C experiments/thread_count run  # Writes results/latest.csv
make -C experiments/thread_count world-sweep  # Five 8-material X×X×100 worlds
make -C experiments/thread_count capacity-sweep  # X×X×200, one thread/tile, until 60 ticks/s
make -C experiments/transition_variations test  # Host checks for five transition variants
make -C experiments/transition_variations       # Build the combined CUDA experiment
make -C experiments/transition_variations run   # Whole-world tick timing, one thread/tile
make -C experiments/transition_variations sweep # 120 world/material/transition configurations
make -C experiments/transition_variations profile  # Separate Nsight Compute counters + report
```

See the experiment README for parameters, GPU verification, and the measured
RTX 2070 timing and cache-hit tables at 8, 10, 12, and 14 materials. The dependent
lookup update reaches about 0.071–0.078 ms per tick at 100x100x100 and 1.71–2.00 ms
at 500x500x100. Fourteen materials require a 5.50 GiB table and are the largest
full table that fits in 8 GiB. Cache profiling shows low hit rates despite this
throughput. Every world-size run records its VRAM estimate and current memory
checks before allocation.

At 8 materials and one thread per tile, the 200-high sweep reaches 66.5 ticks/s
at 1000×1000×200 and falls to 54.9 ticks/s at the next step, 1100×1100×200.

The README and experiment documentation describe the current state and are
maintained in place. Raw run artifacts belong beside their experiment, rather
than becoming the main project index.
