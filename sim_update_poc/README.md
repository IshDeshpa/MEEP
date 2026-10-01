# Sim Update Strategy Proof-of-Concept
- In-place updates. Naive strategy; would mean object updates are not deterministic (races between object update threads may produce different updates)
- Buffer switching. Suppose two buffers exist, one at the current timestep and one at the timestep ahead. At the edge of each timestep, calculate updates for every block based on currently adjacent blocks that fill the other buffer. Allows for deterministic calculations.
- Cell-based or entity-based?
  - We could loop through every cell and determine if it has any blocks around it, and calculate the block that should go there the next iteration
  - We could loop through every entity (block) and determine where it should move/how it should change, based on adjacent entities
- Pipelined buffering? start calculating two steps ahead as we generate the first next_state (we also generate next_next_state)
