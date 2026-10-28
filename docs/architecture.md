# MEEP architecture working model

Current discussion: 2026-10-27. This is the project's living architecture note.
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

The initial material set, world size, frame rate, boundary behavior, and class
requirements for CPU threading or renderer implementation remain open. Dirty
scheduling is optional; the assistant recommends full-world updates initially.
The status of probabilistic transitions should be confirmed separately.

## Current implementation and evidence

- `src/state/state.hpp`: flat z-fastest world indexing
  `z + k * (y + m * x)`; one-byte Cell with 4-bit material and 4-bit state.
  The spare bits are reserved; age is a post-alpha proposal. A face LUT uses
  65,536 uint16 entries.
- `src/state/state.cu`: two device-only arrays allocated with cudaMalloc.
  Initial-state upload and a synchronization protocol are not yet implemented.
  CUDA calls use the host-side `CUDA_CHECK` helper in `src/extra/check.hpp`,
  which throws with call/error/source context without synchronizing. Allocation
  failure releases any first buffer already allocated; destructor cleanup stays
  non-throwing. Execution errors from asynchronous work still need a checked
  completion operation at the appropriate synchronization boundary.
- `src/main.cpp`: initialization and scheduling comments, not a running
  simulation loop. Comments propose partition `(tick >> 1) & 1` and orientation
  `tick & 1`: x faces, z faces, then change partition and repeat.
- `src/render/render.cu` is a CUDA hello-world stub.
  `experiments/generic_renderer` is a CPU software-rendering prototype that
  writes images; it is not an OpenGL implementation.
- The thread-count and transition-variation experiments measure synthetic
  rules on fixed partitions. Face/edge experiments support exploring small
  LUTs but neither prove material behavior nor include shifted-partition costs.
  Their six-face and twelve-edge workloads differ from the whiteboard's two
  lookups. See their READMEs for actual measurements and qualifications.

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
axis is y; its sign and boundary handling still need an explicit convention.

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

## Thread and tile coordinate mapping

The user's cell diagram numbers local cells 1..8 with z changing fastest:
`label = 1 + 4*dx + 2*dy + dz`, where each offset is 0 or 1.
World storage also uses z fastest, followed by y and then x. The diagram's faces, in top-left/top-right/bottom-left/
bottom-right order, are x: [3,4,1,2], [7,8,5,6]; z: [4,8,2,6], [3,7,1,5].
Both face orientations put increasing y on top and use the same local
horizontal/vertical convention.

The current launch mapping uses a 1D CUDA grid with one thread per full 2x2x2
tile. CPU `WorldState::update_world` launches `update_tile` with a bounds check
for the last partial block. The kernel computes tile-grid coordinates tx,ty,tz,
converts them to world origins, and gathers eight cells with z-fastest strides.
The kernel now calls `state_transition(tile, face_phase)` on that same thread;
its rule body remains a stub. Output writes, boundary preservation, and buffer
swapping are still unimplemented. The helper launches no additional threads.

Let n,m,k be world sizes along x,y,z and p = (tick >> 1) & 1.
The current work-in-progress code selects only non-wrapping tiles:
Tx = (n-p)/2, Ty = (m-p)/2, Tz = (k-p)/2, using integer division.
Assume positive dimensions and checked arithmetic. If any tile count is zero,
skip the tile launch; do not divide by zero or launch zero blocks.

Flatten CUDA thread identity:
`tile_id = size_t(blockIdx.x) * blockDim.x + threadIdx.x`.
Return when tile_id >= Tx*Ty*Tz. Enumerate tile coordinates z fastest:
`tz = tile_id % Tz`, `ty = (tile_id / Tz) % Ty`,
`tx = tile_id / (Tz*Ty)`.
The tile's cell origin is `(x0,y0,z0) = (2*tx+p, 2*ty+p, 2*tz+p)`.

World cell addressing is `index(x,y,z) = z + k*(y + m*x)`.
Let base = index(x0,y0,z0), sx = m*k, sy = k, sz = 1.
Diagram cells 1..8 then have addresses:
`[base, base+1, base+sy, base+sy+1,
  base+sx, base+sx+1, base+sx+sy, base+sx+sy+1]`.
A tile ID is not a world-cell index and is not incremented to access its cells.

For an 8x6x4 world at p=0, the tile grid is 4x3x2.
Tile 5 has tile coordinates (0,2,1), world origin (0,4,2), and base index 18.
Its diagram-ordered addresses are [18,19,22,23,42,43,46,47].

This mapping excludes boundary cells when they do not fit in a complete tile.
For even dimensions, partition 1 leaves the outermost layer on all six sides
outside the tile updates. Those cells must still be correctly populated in
next before swapping buffers. Preserving them makes them static under that
tick, not automatically solid walls. A final boundary rule is still open.
Periodic boundaries instead require different tile counts and per-coordinate
wrapping; simply reusing the additive addresses above at an edge is incorrect.

## Cell representation and future aging

Confirmed layout: one byte per cell, with a 4-bit material ID and four reserved
bits. Avoiding a packed two-cell representation is intentional for initial
simplicity. The spare bits may become age; they are not yet a fixed alpha field.
Two 4-bit cells can be packed into a byte using masks/shifts, but lack independent
byte addressing and may share a write unit. Revisit packing only when useful.

`CubeTile` is now a struct containing `Cell cells[8]`, with an eight-byte size
assertion. The earlier uint64_t representation and pack_cube/unpack_cube helpers
have been removed from the current source. The transition takes a CubeTile value
and a face phase and returns a CubeTile; the material LUT calls are unfinished.

The header now provides CPU/GPU-callable constexpr pack_face and unpack_face
helpers. pack_face reads four selected `.elem` fields at shifts 12,8,4,0;
unpack_face writes the four returned nibbles to the specified output cells.
state_transition uses [2,3,0,1] and [6,7,4,5] for x faces, or [3,7,1,5] and
[2,6,0,4] for z faces, for both packing and unpacking. LUT calls are placeholders,
so the face path currently returns the input unchanged. next_tile starts as a
copy of the input; unpacking replaces only elements and preserves reserved
state bits at their existing positions. This does not implement age-preserving
movement, which remains outside alpha. World output writes are still pending.


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

Current implementation: the CPU member `WorldState::update_world(tick)` owns
launch preparation. The free CUDA kernel `update_tile` receives current/next
device pointers, m/k cell strides, tiles_y/tiles_z, tile_count, and both phases.
The input pointer is const. n and tiles_x are not separately needed by this
kernel: total tile count and the y/z grid extents determine its x coverage.

CUDA __global__ kernels cannot be class members. __device__ helper methods
can be members, but their object must be accessible to device code. A CPU
WorldState object containing device pointer values does not itself become
device-accessible; do not pass its ordinary host this pointer to the kernel.

Explicit scalar/pointer parameters are the current small-step choice.
Assistant recommendation if the argument list grows: pass a small trivially
copyable, non-owning device view by value, containing device pointers and
dimensions, plus per-launch phases/grid metadata. This copies metadata and
pointer values, not world contents. Keep ownership/destruction in the CPU
class; its host world pointer and allocation logic do not belong in the view.
A view may have __device__ or __host__ __device__ indexing helpers.

Choose individual parameters versus a view by interface size, reuse across
kernels, and clarity. Neither is automatically faster; inspect generated code
and measure if parameter handling matters. Device-resident shared metadata is
a further option for large/dynamic state, with explicit update ordering and
lifetime requirements. Avoid global/constant symbols for changing per-world
launch state by default. Pass partition and face phase by value per launch.
Source: [CUDA class and kernel restrictions](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-support.html#function-members).

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
- **Boundary coverage:** even/odd tilings need an explicit boundary policy.
  Periodic wrapping is simple for reference tests; a bounded material world
  likely needs walls/padding and defined treatment of uncovered cells.
  Require supported dimensions explicitly; positive dimensions alone are not
  sufficient for straightforward 2x2x2 tiling.
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
- What are gravity's sign, boundary behavior, and supported world dimensions?
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

1. Define initial scenes, face rules, coordinate conventions, and boundary
   policy for the confirmed four-tick schedule.
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
