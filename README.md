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
and calls `swapBuffers`. Main constructs a blank world, loads a LUT, and starts the
tick/render threads. See the [architecture model](docs/architecture.md).
World updates (including GPU completion and buffer swaps) and
`stateToWorldLaunch` share a mutex. The renderer releases it after the entity
snapshot is complete; projection, clipping, rasterization, display, and tick
pacing run outside the world lock.

From `src/`, `make run` constructs a 100x100x100 blank AIR world with a BLOCK
shell. Use `make run ARGS="-n 20 -m 10 -k 30"` to change blank dimensions, or
`make run ARGS="--world worlds/world_water_100x1x100.bin"` to explicitly load the
included WATER scene. File worlds supply their own dimensions; combining
`--world` with dimension options is an error. Saved worlds live in `src/worlds/`.
The `gen_world` tool writes there regardless of the working directory, using
descriptive filenames such as `world_water_sphere_5x5x5.bin`:
`make gen_world ARGS=water` (an optional output filename is accepted).

For custom dimensions, sphere radii, centers, or multiple spheres, use JSON:

```json
{
  "dimensions": [30, 20, 30],
  "spheres": [
    {"center": [8, 6, 8], "element": "water", "radius": 4},
    {"center": [20, 6, 20], "element": "lava", "radius": 3}
  ]
}
```

Run `make gen_world ARGS="--json /path/to/scene.json my_world.bin"` from
`src/`. The output goes to `src/worlds/` (default `world_spheres.bin`). Centers
are zero-based visible-world coordinates; spheres are clipped at world edges,
and later spheres overwrite earlier ones where they overlap. Empty space is AIR.

`make lut` (or `make alpha-lut`) generates `build/lut_alpha.bin` from `state/lut_gen_alpha.py`
(65,536 little-endian uint16 entries, 128 KiB). Application and blank-world test
builds generate it automatically when missing or older than the generator.
`make run` explicitly passes that alpha LUT at runtime; `run-world_blank` also
defaults to alpha. The application accepts `--lut FILE` to select another
compatible table without rebuilding, for example
`./main --lut /absolute/path/to/rules.bin` or
`make run ARGS="--lut /absolute/path/to/rules.bin"`. Direct `./main` execution
also defaults to alpha using its absolute source-tree path. The old `src/lut.bin`
is unused unless explicitly selected at runtime.

Click the window to capture input; Escape releases it. Default mouse behavior
retains the original input conversion and pitch rule, with sensitivity
`0.00001` for normal builds and `0.002` for `XPRA_MOUSE=1` builds. Xpra users
can keep using `make -C src run XPRA_MOUSE=1`.

WSL's absolute pointer workaround is selected at compile time:
`make -C src run WSL_MOUSE=1 ARGS="--world worlds/world_lava_sphere_5x5x5.bin"`.
This defines `MEEP_WSL_MOUSE`; only this build converts absolute positions to
signed pixel deltas, preserves fractional movement, resets position baselines
on capture changes, and clamps
pitch before a flip. It defaults to sensitivity `0.002`.
Use `--mouse-sensitivity VALUE` to override the build's sensitivity without
rebuilding; `0` disables mouse look. Defaults live in `src/config.hpp`.
`make -C src test-mouse` checks sensitivity defaults; add `XPRA_MOUSE=1` to
check Xpra defaults or `WSL_MOUSE=1` to also check WSL input conversion without
CUDA hardware or an X display. `WSL_MOUSE` and `XPRA_MOUSE` are mutually
exclusive and default to `0`.
Changing either option rebuilds affected objects automatically.

Run `make` without `-j` for serial builds in a small WSL instance. Editing
`config.hpp` rebuilds most CUDA files, and bare `-j` launches unlimited compiler
jobs. Direct `./main` runs the existing binary and cannot pick up
compile-time setting changes until a build succeeds.

Debug levels are selected with `make DEBUG=<level>` (also works with `run`):

| Level | Defined flags | Behavior | Objects |
| --- | --- | --- | --- |
| `0` (default) | None | Optimized release | `build/` |
| `1` | `MEEP_DEBUG` | General debugging, including tick diagnostics | `build/debug-1/` |
| `2` | `MEEP_DEBUG`, `MEEP_DEBUG_RENDER_TRACE` | General debugging plus renderer buffer dumps and per-frame PPM writes | `build/debug-2/` |

The flag lists live together in `DEBUG_FLAGS_<level>` at the top of
`src/Makefile`. Level 2 inherits level 1. Add a flag to the appropriate row, or
add a row for another level; validation, compiler definitions, build directories,
and `make help` derive from that table. Changing flag lists rebuilds affected
objects, and switching levels relinks `main` automatically. All nonzero levels
use `-O0 -g` and CUDA `-G`, which still has a performance cost.

`make run-world_blank` loads the alpha LUT and checks seven GPU ticks
on four blank worlds, including 100x100x100. It verifies AIR interiors, BLOCK
padding, state bits, output coverage, LUT upload, and buffer parity using
`updateWorld`, `waitForTick`, and `swapBuffers`. Supply another LUT with
`ARGS=/absolute/path/to/rules.bin`. Set `CUDA_HOME` for a nonstandard toolkit.
The older `world_padding` test still needs its call sites adapted to the LUT API.

`make run-render_mask_lifetime` checks triangle-mask storage across 1,024
radius-5 water-sphere snapshots, eight full CUDA render passes, and an empty
snapshot. It verifies mask pointers and visibility against a CPU reference.
Like other application builds, add `WSL_MOUSE=1` for the WSL build configuration.

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
