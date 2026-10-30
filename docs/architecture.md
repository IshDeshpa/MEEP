# MEEP architecture working model

Current discussion: 2026-10-29. This is the project's living architecture note.
Revise the relevant sections as decisions change; replace superseded proposals
rather than appending conversation history. Keep confirmed requirements,
implemented behavior, proposals, and unresolved questions distinct.

## Alpha objective and confirmed scope

The alpha is a visual experiment: determine whether repeated two-face updates
produce movement and material behavior worth developing further. Load a fixed
initial world from memory and display its evolution. Interactive world editing,
aging, and the two-tier transition scheme are outside alpha.

The current tentative rule schedule is confirmed below. Horizontal y faces are
omitted deliberately for this prototype. Visual naturalness is an empirical
judgment for the user/team; analytical correctness or benchmark speed cannot
establish it. CPU/GPU reference agreement remains an implementation check,
separate from whether the resulting behavior looks good.

The initial material set, world size, frame rate, and class requirements for
CPU threading or renderer implementation remain open. Dirty
scheduling is optional; the assistant recommends full-world updates initially.
The status of probabilistic transitions should be confirmed separately.

## Current implementation and evidence

- `src/state/state.hpp`: one-byte Cells, an eight-cell CubeTile, and
  DeviceWorldView readTile/writeTile helpers using visible-world coordinates.
  Renderer-facing dim/coord aliases, Element::COUNT, signed index(coord), and
  const-correct operator()(coord) are retained from the teammate integration.
  Extra Cell bits remain reserved.
- src/state/lut.hpp and lut.cu contain the face types, masks, and packing
  helpers moved from state. LUT loads a 131072-byte binary table into device
  memory and owns its allocation. DeviceLUTView carries a read-only device
  pointer by value and exposes lookup; no separate device view allocation is needed.
- `src/state/state.cu`: the constructor keeps requested n/m/k and padded
  dimensions n+2/m+2/k+2, creates a padded host image, initializes an AIR interior
  and BLOCK shell, and uploads the same image into both device buffers.
  Allocation sizes and current free VRAM are checked before allocating.
  The host image is initialization data, not a live simulation mirror.
- `WorldState::updateWorld` schedules tiles using padded dimensions.
  The kernel reads a CubeTile, packs/unpacks two faces through material LUT
  calls, and writes the result. BLOCK immobility belongs entirely to the LUT.
  Both phases cover every visible cell. The caller waits for completion and
  calls swapBuffers before submitting the next tick.
- `src/main.cpp`: the CPU ticker submits a tick, waits for stream completion,
  and sleeps until its requested deadline. main does not yet start the ticker.
- CUDA calls use the teammate's CPU-facing src/extra/check.hpp declaration
  and src/extra/check.cu implementation; failure paths release partially
  allocated buffers. GPU completion errors are checked by waitForTick.
- `src/render/render.cu` is a CUDA hello-world stub; the generic renderer
  experiment is a CPU image-writing prototype. Neither yet renders live worlds.
- Scheduling experiments still use synthetic rules and fixed partitions.
  Their six-face/twelve-edge workloads do not measure the alpha's four-tick
  schedule, its padding, or complete application performance.

## Whiteboard overview

CPU initialization creates world state and starts renderer/ticker activities.
The ticker measures a tick and waits for the remainder of a desired period.
The broader whiteboard includes aging, probabilistic transitions, dirty-tile
selection, tile updates, dirty propagation, and completion before the next tick.
These boxes do not all belong to alpha.

## Confirmed face update schedule

Each tick updates two opposite, disjoint faces in every 2x2x2 tile. The two
lookups within a tile are independent; the next tick consumes the completed
world produced by this tick. One tick is one row of the following cycle.

| Tick modulo 4 | Margolus partition | Faces updated |
| --- | --- | --- |
| 0 | 0, origins even, starting at (0,0,0) | Two x faces, constant x, spanning yz |
| 1 | 0 | Two z faces, constant z, spanning xy |
| 2 | 1, origins odd, starting at (1,1,1) | Two x faces |
| 3 | 1 | Two z faces |

Then repeat. Partition is `(tick >> 1) & 1`; orientation is `tick & 1`.
The partition changes every two ticks; ping-pong buffer roles change every tick.
The y faces, spanning horizontal xz planes, are tentatively omitted. The vertical
axis is y; its sign still needs an explicit convention. Partition origins
refer to padded coordinates under the confirmed BLOCK-shell boundary policy.

This is a distinct update method from both earlier six-face variants:
independent within each tick's disjoint face pair, sequential between ticks,
with partition changes after each x/z pair. The old independent variant reads
the original tile for all six faces and later overlapping proposals overwrite
earlier ones. The old sequential variant processes all six faces in one tick.
Neither benchmark measures this new four-tick schedule.

A GPU worker can load one cube, perform its two independent face lookups, and
store the eight cells. Independence permits parallel execution but does not
require separate threads or guarantee simultaneous instructions. One thread
per face is another valid mapping, to compare later if needed.

A dirty-bit proposal records whether either the x or z tick changed cells in a
tile, then derives affected tiles for the next partition. Preserve that proposal
as an optimization question rather than an alpha requirement.

## Padded world storage

Confirmed boundary policy: exactly one unreactive BLOCK cell on each of the six
faces, including edges/corners. Requested/visible dimensions remain n,m,k;
actual allocation dimensions are padded_n=n+2, padded_m=m+2, padded_k=k+2.
The shell is initialized in the CPU image and both GPU buffers; the interior
defaults to AIR until scene-loading functionality is added.

WorldState::index(x,y,z) maps visible coordinates to the padded allocation.
WorldState::paddedIndex(x,y,z) addresses raw storage coordinates for setup or
diagnostics. Rendering must iterate only the requested dimensions and use the
visible coordinate mapping; index(coord) and operator()(coord) also accept
the signed -1/high-edge halo needed for renderer neighbor queries and reject
coordinates outside it. Const WorldState access returns const Cell&.
These accessors read the CPU initialization image, not a synchronized live copy.
The shell must not become rendered voxels or shift
visible-world positions. Live-world rendering is not yet integrated.

Partition phase 0/1 refers to origins in padded coordinates. Every visible cell
belongs to a complete tile in both phases, for positive even or odd dimensions.
Only shell cells can remain uncovered; since both buffers initialize them as
BLOCK and LUT rules keep BLOCK unchanged, they need no per-tick copy. Normal
tile writes are unconditional: there is no special BLOCK branch in the kernel
or buffer helpers. LUT generation must preserve BLOCK locations and immobility.

Initialization checks padding addition, allocation multiplication, signed
coordinate range, and current free GPU memory for both buffers. The CPU image
uses owned array storage and is released with delete[]. No wraparound is used.

The existing world_padding test covers visible/padded indexing and tile helper
access, but its call sites and identity-rule fixture need adaptation to the LUT
API before that target can run again.

The blank-world regression is `make -C src run-world_blank`, using the actual
`src/state/la.bin` by default (override with `ARGS=/absolute/path/to/rules.bin`).
It checks all AIR/BLOCK reference faces, exact LUT upload, both initial GPU
buffers, and seven ticks on 1x1x1, 3x4x5, 10x14x18, and 100x100x100 worlds.
Destination interiors are poisoned before each tick to expose missing writes;
every output cell is compared with an independently constructed blank CPU
reference. Each tick waits and calls production swapBuffers. RTX 2070
verification and Compute Sanitizer memcheck passed with zero errors/leaks.
This establishes blank-world stability, not nonblank material behavior.

## Thread and tile coordinate mapping

The diagram's zero-indexed local labels are `c = 4*dx + 2*dy + dz`.
Face order is top-left, top-right, bottom-left, bottom-right:
x faces [2,3,0,1] and [6,7,4,5]; z faces [3,7,1,5] and [2,6,0,4].
Both face orientations put increasing y on top.

The launch uses one thread per complete 2x2x2 tile in padded storage:
`Tx = (padded_n-p)/2`, `Ty = (padded_m-p)/2`,
`Tz = (padded_k-p)/2`, where `p = (tick >> 1) & 1`.
The final partial CUDA block is bounds-checked against Tx*Ty*Tz.

Flatten the 1D launch as
`tile_id = size_t(blockIdx.x) * blockDim.x + threadIdx.x`.
Enumerate tiles z fastest:
`tz = tile_id % Tz`, `ty = (tile_id / Tz) % Ty`,
`tx = tile_id / (Tz*Ty)`.

Padded origins are (2*tx+p, 2*ty+p, 2*tz+p).
The kernel converts these to signed visible-world origins by subtracting one
per axis, then calls `buffers.readTile(x,y,z)` and
`buffers.writeTile(x,y,z,next_tile)`.
An origin of -1 includes low padding and visible cell 0. An origin of n-1
includes visible cell n-1 and high padding n along x; analogously for y/z.
Callers must supply origins whose full tile lies within the allocation.

DeviceWorldView hides the padding offset and physical strides:
`index(x,y,z) = (z+1) + padded_k*((y+1) + padded_m*(x+1))`.
Offsets for local cells 0..7 are
`[0,1,sy,sy+1,sx,sx+1,sx+sy,sx+sy+1]`,
with `sy=padded_k` and `sx=padded_m*padded_k`.

For a requested 6x4x2 world, storage is 8x6x4. At p=0 the tile grid is 4x3x2.
Tile 5 has tile coordinates (0,2,1), padded origin (0,4,2), visible-world origin
(-1,3,1), and base index 18. Its eight addresses are
[18,19,22,23,42,43,46,47].

## Cell representation and future aging

Confirmed layout: one byte per cell, with a 4-bit material ID and four reserved
bits. Avoiding a packed two-cell representation is intentional for initial
simplicity. The spare bits may become age; they are not yet a fixed alpha field.
Two 4-bit cells can be packed into a byte using masks/shifts, but lack independent
byte addressing and may share a write unit. Revisit packing only when useful.

`CubeTile` is now a struct containing `Cell cells[8]`, with an eight-byte size
assertion. The earlier uint64_t representation and pack_cube/unpack_cube helpers
have been removed from the current source. The transition takes a CubeTile value
and a face phase and returns a CubeTile using the loaded material LUT.

lut.hpp declares CPU/GPU-callable packFace and unpackFace helpers;
their runtime definitions are in lut.cu. packFace reads four selected `.elem` fields at shifts 12,8,4,0;
unpackFace writes the four returned nibbles to the specified output cells.
stateTransition uses [2,3,0,1] and [6,7,4,5] for x faces, or [3,7,1,5] and
[2,6,0,4] for z faces, for both packing and unpacking. LUT lookups determine the returned face materials. next_tile starts as a
copy of the input; unpacking replaces only elements and preserves reserved
state bits at their existing positions. This does not implement age-preserving
movement, which remains outside alpha. The kernel now scatters the result to the next world buffer.


The future age proposal includes fire producing hot air that rises relative to
normal air and later becomes normal air; steam aging into water; fire into air;
and ice into water. These are desired stylized rules, not alpha requirements.
Movement must carry element and age together.

## Proposed CPU and GPU responsibilities

Recommendation for alpha: the GPU owns the live simulation state; the CPU
initializes, schedules, manages timing and display, and handles diagnostics.

| Responsibility | Proposed owner and mechanism |
| --- | --- |
| Initial state and rule construction | CPU, followed by upload; GPU generation is also possible |
| Live current/next world and LUTs | GPU global memory |
| Tile enumeration and cell updates | One bulk CUDA kernel per substep; initially one logical thread per tile |
| Step order and metadata | CPU coordinator submits ordered work to one CUDA stream |
| CPU reference | Separate small-world implementation for validation |
| Rendering | Consume a completed world version through an explicit handoff |
| Future edits | CPU builds commands; GPU applies them at a defined step boundary |

A CUDA launch describes a grid of work; it does not create an OS thread per
tile. Hardware schedules that grid in batches. Start with a full-world launch,
not CPU enumeration of dirty tiles, per-tile host threads, or device-side
kernel spawning. A single CPU coordinator is enough to drive this; any required
CPU parallelism should serve real work such as rendering, loading, or reference
validation. Confirm course requirements before choosing the final thread model.

The world buffers now use explicit device allocations; a one-time initial-state
upload is still needed. CPU access requires an explicit transfer or another
defined handoff. CUDA block-local shared memory is a different facility and is
not a world store shared with the CPU.

## Kernel parameters and class ownership

The CPU member `WorldState::updateWorld(tick)` owns launch preparation.
It passes a fresh `deviceView()` by value, along with tiles_y/tiles_z,
tile_count, partition phase, and face phase. DeviceWorldView contains current
and next device pointers and the padded y/z dimensions used as strides.
This copies metadata and pointer values, not world contents. Reacquire a view
after buffer roles change; an existing view retains its captured pointers.

The view exposes index(x,y,z), readTile(x,y,z), and
writeTile(x,y,z,tile). They take visible-world coordinates, apply padding
internally, and perform no material filtering. The current-buffer pointer is
read-only. The helpers are CPU/GPU-callable methods defined in state.cu;
their declarations use MEEP_HOST_DEVICE from extra/cuda_compat.hpp.
Both C++ and CUDA callers see the complete DeviceWorldView declaration.
The macro retains CUDA annotations under nvcc and expands to nothing for
ordinary C++ compilation. Dereferencing
device pointers is valid only from an execution space that can access them.

The owning WorldState remains on the CPU and manages allocations/destruction.
A CPU object containing device pointer values does not itself become
device-accessible. CUDA __global__ kernels cannot be class members; __device__
helpers can be. The small, trivially copyable view separates device access from
ownership and avoids passing an ordinary host this pointer to a kernel.

Phases remain per-launch values. No performance advantage over individual
parameters is assumed; inspect generated code and measure if this matters.
Source: [CUDA class and kernel restrictions](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-support.html#function-members).

## Tick pacing

worldTick remains a CPU loop. TickRate enum values represent integer
milliseconds per tick; zero means Unlimited. Each iteration snapshots its period,
records a steady-clock start, submits updateWorld, waits for GPU completion
through WorldState::waitForTick, calls swapBuffers, and increments the completed tick counter.
For nonzero periods it calls std::this_thread::sleep_until(start + period).
The effective remaining sleep is max(0, period - elapsed), including GPU work.

Overruns start the next iteration immediately without accumulated catch-up
ticks. Unlimited removes deliberate pacing but still waits for each GPU tick,
bounding outstanding simulation work to one tick. OS scheduling can wake a
sleeping thread late. The 17 ms and 34 ms enum values target approximately
58.82 and 29.41 ticks/s respectively, not exactly 60 and 30.

waitForTick currently synchronizes the same default CUDA stream used by
updateWorld and checks asynchronous errors. It is not a device-wide wait,
but legacy-default-stream dependencies can still couple it to other work.
Use a dedicated simulation stream and explicit rendering handoffs before
expecting independent GPU simulation/rendering. A separate CPU thread alone
does not establish GPU independence or pin that thread to a CPU core.

The ticker is still not started by main, which currently requests lut.bin
relative to its working directory. Default padded-state upload, material LUT
calls, and the post-wait swapBuffers operation are implemented. Scene loading
remains unfinished. A future
cross-thread tick-rate setter needs atomic or locked access; the current rate
is a plain global read by the ticker.

References: [C++ thread sleeping](https://eel.is/c++draft/thread.thread.this)
and [CUDA stream synchronization](https://docs.nvidia.com/cuda/cuda-programming-guide/03-advanced/advanced-host-programming.html#more-on-streams-and-events).

## Proposed step and ownership contract

1. Upload the initial world and LUT. Track dimensions, substep index, partition,
   orientation, and the buffer containing the latest completed state.
2. For the selected partition, each worker gathers its tile from current.
3. Apply the selected material rule in local values; scatter the result to next.
4. Every output cell has one writer. Write unchanged cells too, including any
   cells outside the active boundary tiling that must be preserved.
5. Complete the substep before its output is consumed by another substep or
   renderer. Work submitted to the same CUDA stream supplies device ordering;
   the CPU need not block after every launch.
6. Advance the schedule and buffer roles. Host-side pointer bookkeeping can
   happen after enqueueing, but it must not falsely publish GPU completion.
7. Hand a completed version to the renderer. Do not reuse its storage for
   writing until all readers have finished.

Start with flat one-byte-per-cell arrays. Gather/scatter shifted neighborhoods
by coordinates instead of repacking the whole world between partitions.
Use explicit byte masks/shifts for a portable serialized layout rather than
relying on C++ bitfield order. Benchmark more aggressive packing later.

Double buffering remains the conservative baseline. An in-place variant is
possible if each substep reads and writes only its own disjoint tile, kernels
are ordered, and no renderer or other pass reads a changing world. That is a
legitimate later alternative; it loses the stable previous-state buffer and
must be revisited if rules read outside their tile.

## Problems to resolve or avoid

- **Fixed partition:** isolated tiles never exchange material. Shift the
  partition and test movement across old tile boundaries.
- **Boundary coverage:** the one-cell BLOCK shell resolves omitted visible
  edge cells in both partitions. Keep logical coordinates distinct from padded
  indices and initialize both buffers. LUT rules must keep BLOCK static; do
  not hide a broken rule behind material-specific buffer-write filtering.
- **Incomplete next buffer:** skipping clean tiles without copying them or
  another validity scheme exposes state from an older tick after swapping.
- **Dirty does not mean permanently inactive:** a tile unchanged under x rules
  may change under z rules or a shifted partition. Time, age, random trials,
  emitters, and neighbors can also reactivate it. Track dependencies and phase
  validity, not just whether the last output differed.
- **Dirty propagation is spatial:** a changed old tile can affect several new
  tiles. A partition shift can intersect up to eight new 2x2x2 tiles. Map changed
  cells to future rule inputs; do not reuse old tile indices blindly. Packed
  dirty-bit writes also need atomics or an equivalent ownership scheme.
- **Overlapping face proposals:** the confirmed disjoint face pair avoids
  the earlier six-face overwrite conflict. Preserve that disjointness; adding
  overlapping independent proposals would require explicit conflict semantics.
  Fixed x/z ordering can introduce directional bias; assess it in the prototype.
- **Face key orientation:** the experiment's x-face extraction orders cells
  with y as the low coordinate bit; its z-face extraction uses x as the low
  bit and y as the high bit. Reusing one gravity-aware LUT with these raw
  orderings would give its axes different physical meanings. Normalize both
  face keys to the same local horizontal/vertical convention and invert that
  mapping on output, or use deliberately orientation-specific tables.
  Omitting y faces does not eliminate horizontal movement: x faces permit
  z/y movement, while z faces permit x/y movement. The visual effect of this
  alternation still needs evaluation.
- **Lock lifetime:** unlocking a CPU mutex after launching an asynchronous GPU
  operation does not mean that operation finished. Atomic pointer publication
  neither completes GPU work nor protects a buffer against later reuse.
- **Tick pacing:** timing a launch measures submission, not GPU execution.
  Use GPU events for kernel time. Define overrun/catch-up behavior, clamp any
  remaining delay, and avoid an unbounded queue of future ticks.
- **Cell metadata:** if age/state travels with material, movement must move
  those bits too. A material-only LUT output cannot silently erase metadata.
  Making all metadata part of a dense LUT key can explode its size.

## Rule representation alternatives

The alpha will evaluate four-cell face LUTs. Smaller edge LUTs, full cube LUTs,
and direct rule code remain broader alternatives, not substitutes for this
specific visual experiment. Synthetic edge performance does not answer whether
the selected face movement is satisfactory.

Keep table key size, number of entries, and entry width distinct. With M material
IDs and dense material-only indexing:
- Four-cell face: M^4 entries; a four-ID result needs 16 bits at 4 bits per ID,
  so the current uint16 representation uses 2 * M^4 bytes.
- Eight-cell cube: M^8 entries; an eight-ID result needs 32 bits,
  so uint32 results use 4 * M^8 bytes.

At 16 materials, those sizes are 128 KiB and 16 GiB respectively.
A separate two-byte probability per cube entry would add 8 GiB.
These are calculated sizes, not measured performance. Actual allocation also
includes both worlds, rendering resources, current VRAM use, and headroom.
The full 16-material cube LUT is unsuitable for the documented 8 GiB RTX 2070.

The current nibble-key face LUT reserves all 16^4 entries even when fewer
materials are used. Reaching exactly M^4 entries for M < 16 requires dense
base-M encoding rather than uncompressed four-nibble indexing.

## LUT ownership and memory placement

Current code: lut.hpp contains the face type, table alias, nibble masks,
pack/unpack declarations, and the user's simple LUT/DeviceLUTView pointer
structs. lut.cu contains only the face packing/unpacking definitions moved from
state.cu. File loading, GPU allocation/cleanup, lookup, and world-tick wiring
remain unimplemented.

Intended design, not an implementation request: a CPU owner can use a
constructor/destructor for its GPU allocation and expose a trivial read-only
view passed by value. The owner must outlive every GPU use. The generator's
file format is 65,536 little-endian uint16 entries (131,072 bytes).
Any future loader can use temporary CPU staging and a one-time upload.

Keep structural moves separate from feature implementation: do not implement
the LUT owner or lookup just because its planned interface is being discussed
or its source files are being reorganized.

Confirmed source layout: state.hpp and lut.hpp include extra/cuda_compat.hpp.
That shared header defines MEEP_DEVICE and MEEP_HOST_DEVICE using one
__CUDACC__ conditional: CUDA annotations under nvcc, empty replacements for
ordinary C++ compilation. Neither state nor LUT headers need surrounding CUDA
guards. Do not redefine CUDA's reserved annotation names. Empty macros do not
provide CPU implementations of device-only functions.

All method bodies remain in .cu files with their execution annotations,
organized by responsibility. Private updateTile and stateTransition
declarations stay inside state.cu's anonymous namespace.

CUDA execution annotations remain on public device-callable declarations.
A local NVCC 12.6 check accepted a later host/device redeclaration with a
warning in the same translation unit, but a separate GPU caller seeing only
the unannotated declaration failed as a call to a host-only function.
Definition-only annotations therefore do not support the separate callers.

Runtime helpers do not need constexpr/inline for optimization. constexpr
permits constant evaluation and imposes inline definition-visibility rules.
The build enables -rdc=true and -dlto during CUDA compilation and linking,
allowing out-of-line helpers and cross-file device optimization after moving
face helpers to lut.cu. Inlining remains a compiler decision.

The merged Makefile preserves CUDA host-compiler selection, ZLUDA options,
renderer definitions/assets, and distinct .o/.cu.o names for same-stem sources.
CUDA tests build as separate executables rather than being linked into main.
WorldState exposes one updateWorld entry point; there is no compatibility
wrapper. Functions use lowerCamelCase, and parameters/local variables use
snake_case throughout main, state, and LUT code.

Validation: application build/startup, six-size padded-world GPU regression,
signed renderer halo access, and geometry loading pass. LUT-specific loading
and lookup tests are not part of the current code.
Sources: [CUDA separate compilation](https://docs.nvidia.com/cuda/cuda-compiler-driver-nvcc/#using-separate-compilation-in-cuda)
and [C++ constexpr rules](https://eel.is/c++draft/dcl.constexpr).

C++ const restricts writes through an interface; it does not select a CUDA memory
space. cudaMalloc storage remains global memory. CUDA __constant__ arrays use
a separate 64 KiB space, so the current complete face LUT cannot fit.
Putting only a pointer in __constant__ storage does not move its pointees there.
Constant-memory requests serialize for distinct addresses within a warp, making
access patterns important even for a smaller table that fits.

Start with read-only global-memory lookup. Explicit texture fetching and
read-only-cache load choices are later profiling alternatives; const alone
does not create a texture. No cache-hit or throughput advantage is claimed.
A device-global pointer would avoid explicit parameters but introduce hidden,
per-device shared configuration; prefer the view for ownership and testability.

Source: [CUDA memory guidance](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#constant-memory).

## Two tier transitions after alpha

Purpose: preserve per-cell age during movement while allowing reactions to
create new elements with fresh ages. This is not primarily a table-compression
proposal, and it is distinct from doing two face lookups in one tick.

1. Index a dispatch table using only the tile's material IDs.
2. The descriptor selects no change, a reaction-table entry, or a movement-table
   entry, and contains the appropriate secondary index.
3. No change preserves cells. A separate aging operation can still change age.
4. A movement entry describes a permutation of source positions. Apply it to
   whole Cells so each element retains its age.
5. A reaction entry supplies the resulting elements, including their changed
   positions, and initializes output ages to their configured maxima.

There are three logical tables but at most two lookup levels. A staged gather
of input Cells is needed when applying a permutation so early writes cannot
destroy later sources. For a four-cell face there are 4! = 24 possible pure
permutations; for an eight-cell cube there are 8! = 40,320. This bounds the set
of movement patterns, not the size of the first material-keyed dispatch table.

Assistant proposal for later consideration: a movement permutation could also
be stored directly as source-position fields in a descriptor, eliminating the
secondary movement lookup. This trades descriptor width against an extra load;
it is not an alpha task.

A reaction that resets every output cell's age may also rejuvenate unrelated
survivors. Decide whether that is desired. Define aging cadence, expiry order,
and zero-age behavior separately so changes to the tick schedule do not silently
change material lifetimes.

### Alternative single table with copy and create actions

The user identifies entry width as a concern: the face table can plausibly grow,
while the full 16-material cube table is already infeasible on available GPUs.
Assistant proposal, not an agreed change or alpha requirement: keep material IDs
as the key, but encode an action for each output cell.

For four-cell faces with 16 materials, each output selects one of 20 actions:
copy input Cell 0..3, preserving its element and age; or create material 0..15,
initializing its age from a small per-material maximum-age table. Five bits
encode each action, so four actions need 20 bits. A simple uint32 entry leaves
12 spare bits. An identity entry copies inputs 0,1,2,3; a movement entry copies
a permutation; a mixed reaction can copy survivors and create new products.
All copies refer to the original loaded input, never a partially written output.

Example: [COPY_1, COPY_0, CREATE_STEAM, COPY_3] swaps the first two whole cells,
creates steam at the third position, and preserves the fourth cell.

Calculated full face-table costs at 16 materials:
- Existing uint16 material output: 128 KiB.
- Packed three-byte action output: 192 KiB, with packing/access complexity.
- Proposed uint32 action output: 256 KiB.

The uint32 design is the preferred candidate for simplicity if this feature is
pursued. It removes a secondary descriptor lookup at the cost of wider entries
and action decoding. No performance advantage is claimed without measurement.
The key still excludes age: this supports transporting age and initializing new
age, not selecting age-dependent reactions. Expiry needs its separately defined
operation. Validate movement descriptors as permutations; unconstrained copies
can duplicate cells if the rule author makes a mistake.

For cubes, each output selects among 8 sources plus 16 new materials, requiring
five bits as well. Eight outputs need 40 bits; tightly packed entries require
20 GiB and uint64 entries require 32 GiB at 16^8 keys. Wider actions therefore
do not solve the cube table's feasibility problem.

The original two-tier scheme remains an alternative, especially if many keys
share a small set of descriptors. A 16-bit dictionary index would allow at most
65,536 descriptors if no bits are reserved for tags; tags/no-change encodings
reduce that capacity unless encoded in descriptors themselves. Its dense cube
dispatch table alone would still take 8 GiB at 16 materials. Deduplicating
descriptors does not by itself eliminate the large first-level key space.

## Rendering alternatives and provisional alpha choice

Keep the synchronization policy separate from whether rendering runs on CPU
or GPU. Rendering must see one completed version, and its reader lifetime must
be honored.

| Alternative | Assessment |
| --- | --- |
| Update, finish, render, then update again | Simplest alpha baseline; no simultaneous access to live state |
| CPU lock held throughout rendering | Can serialize CPU work, but GPU completion still requires explicit synchronization; long stall |
| Complete snapshot for renderer | Straightforward decoupling at a memory/copy cost; source must remain valid until copy finishes |
| Shared GPU resource with fences or map/unmap | Attractive for a GPU renderer; requires a supported graphics API/platform handoff |
| Changed-tile patches to a render copy | Later optimization; needs version tracking, cumulative changes across skipped frames, and a full-resync path |

For the existing CPU renderer, use an explicit completed host snapshot as the
initial bridge, possibly at a lower render rate. Once the copy finishes, the GPU
can continue while the CPU renders that immutable snapshot, provided snapshot
storage is not overwritten while in use. Full simulation/render serialization
is an even simpler first milestone.

For a future GPU renderer, keep the world/render representation on device and
share or derive graphics resources there. If the team intends a custom CUDA
renderer, update and render kernels can initially run in one stream; presentation
of the resulting image is still a separate handoff. OpenGL interop is only an
option, not the current renderer. Verify WSL/platform support before choosing it.

Simulation double buffering alone does not guarantee renderer safety:
if rendering still reads B, the simulation may read B while writing A, but
must not start writing B on the following substep until that reader finishes.
A third buffer or independent snapshot helps overlap but still needs ownership.

## Future user edits

Record input as commands such as paint-region/material, with ordering and a
target tick. Transfer the batch and apply it on GPU before a specified substep.
Resolve overlapping commands deterministically. Do not let the CPU mutate live
world cells while a kernel or renderer is using them. GPU-side edit application
also becomes the place to invalidate active tiles or render regions later.
No interactive edit implementation is needed for alpha.

## Open questions

Before the first integrated simulation:
- Which initial scenes and material rules best reveal whether face movement is
  worth pursuing?
- What is gravity's sign? Padding and coverage now support positive dimensions
  of either parity.
- What world size, tick rate, and frame rate are the initial targets?
- Is the intended renderer CPU software, custom CUDA, or a graphics API?
- Does the class require a particular CPU-threading or rendering approach?
- Are probabilistic transitions included in this prototype?

Visual evaluation, with conclusions based on the rendered prototype:
- Does fixed x-then-z order produce a noticeable preferred direction or rhythm?
- Does omitting horizontal faces limit lateral spreading in an undesirable way?
- Would changing orientation order or rules help, without confusing that change
  with partition changes or a different number of updates per displayed second?
- Use repeatable initial states, rotated x/z scene pairs, and comparable
  simulated durations to make differences inspectable. These reveal artifacts;
  they do not establish a universal definition of natural-looking motion.

Deferred but important:
- How are the four spare bits assigned if aging is implemented?
- Does a reaction reset every output age or preserve unaffected survivors?
- When does aging occur relative to movement, reactions, and expiry?
- Should rendering block, repeat the latest frame, or skip intermediate states?
- When would dirty scheduling save enough work to justify its correctness cost?
- How should aging/probability reactivate apparently unchanged regions?
- Would in-place disjoint updates simplify storage after rendering is settled?
- What command ordering, replay, and snapshot versioning will future edits need?

## Alpha milestones

1. Define initial scenes, face rules, and gravity convention for the confirmed
   four-tick schedule and padded BLOCK boundary.
2. Implement the full-world two-face GPU tick and independent CPU reference.
   Compare every tick across both orientations/partitions and buffer parities.
3. Load a fixed initial world and display its evolution. Judge face movement
   visually; tune or reject the rule/schedule based on observed behavior.
4. Measure simulation, transfer, rendering, and end-to-end times separately.
   Estimate allocations and check current free VRAM before GPU cases.
5. Add concurrency or sparse updates only to address measured limitations.

## Technical references

- [CUDA asynchronous execution](https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/asynchronous-execution.html):
  streams, events, and completion ordering.
- [CUDA unified memory](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/unified-memory.html):
  addressability and platform-dependent concurrency constraints.
- [CUDA graphics interoperability](https://docs.nvidia.com/cuda/cuda-runtime-api/cuda_runtime_api/group__CUDART__INTEROP.html):
  resource ownership and map/unmap synchronization.
