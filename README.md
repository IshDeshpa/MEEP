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
| [CUDA thread-count experiment](experiments/thread_count/README.md) | Compare small grids, occupancy-sized worker grids, and one thread per neighborhood |
| [CPU update proof of concept](experiments/sim_update_poc/README.md) | Serial updates, per-cell threads, and persistent worker pools |

## CUDA experiment

```sh
make -C experiments/thread_count test  # Host checks; no CUDA needed
make -C experiments/thread_count      # Requires nvcc; targets RTX 2070 / sm_75
make -C experiments/thread_count run  # Writes results/latest.csv
make -C experiments/thread_count world-sweep  # Five 8-material X×X×100 worlds
```

See the experiment README for parameters, GPU verification, and the measured
RTX 2070 timing and cache-hit tables at 8, 10, 12, and 14 materials. The dependent
lookup update reaches about 0.071–0.078 ms per tick at 100x100x100 and 1.71–2.00 ms
at 500x500x100. Fourteen materials require a 5.50 GiB table and are the largest
full table that fits in 8 GiB. Cache profiling shows low hit rates despite this
throughput. Every world-size run records its VRAM estimate and current memory
checks before allocation.

The README and experiment documentation describe the current state and are
maintained in place. Raw run artifacts belong beside their experiment, rather
than becoming the main project index.
