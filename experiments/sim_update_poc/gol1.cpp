// single threaded
// results: average of like 0.04ms update_tmr
#include <cstdio>
#include <thread>

#include "gol_config.h"
#include "scoped_timer.h"

void generate_adj_cell(bool state[GRID_SZ][GRID_SZ], int adj[GRID_SZ][GRID_SZ], int i, int j){
  adj[i][j] = 0;

  for (int di = -1; di <= 1; di++){
    for (int dj = -1; dj <= 1; dj++){
      if (di == 0 && dj == 0) continue;

      int ni = i + di;
      int nj = j + dj;

      if (ni >= 0 && ni < GRID_SZ &&
          nj >= 0 && nj < GRID_SZ &&
          state[ni][nj]){
        adj[i][j]++;
      }
    }
  }
}

void generate_adj(bool state[GRID_SZ][GRID_SZ], int adj[GRID_SZ][GRID_SZ]){
  ScopedTimer tmr("adj_tmr");

  for (int i = 0; i < GRID_SZ; i++){
    for (int j = 0; j < GRID_SZ; j++){
      generate_adj_cell(state, adj, i, j);
    }
  }
}

bool determine_live(bool currently_live, int adjacent){
  return adjacent == 3 || (currently_live && adjacent == 2);
}

void print_state(bool state[GRID_SZ][GRID_SZ]){
  for (int i=0; i<GRID_SZ; i++){
    for (int j=0; j<GRID_SZ; j++){
      printf("%s ", state[i][j] ? "\033[94m■\033[0m" : "□");
    }
    printf("\n");
  }

  printf("\n\n");

}

void update_cell(bool prev_state[GRID_SZ][GRID_SZ], bool new_state[GRID_SZ][GRID_SZ], int adj[GRID_SZ][GRID_SZ], int i, int j){
  new_state[i][j] = determine_live(prev_state[i][j], adj[i][j]);
}

void update(bool prev_state[GRID_SZ][GRID_SZ], bool new_state[GRID_SZ][GRID_SZ], double& update_total_ms){
  ScopedTimer tmr("update_tmr");

  int adj[GRID_SZ][GRID_SZ];
  generate_adj(prev_state, adj);

  for (int i=0; i<GRID_SZ; i++){
    for (int j=0; j<GRID_SZ; j++){
      update_cell(prev_state, new_state, adj, i, j);
    }
  }

  update_total_ms += tmr.elapsed_ms();
}

int main(){
  bool state1[GRID_SZ][GRID_SZ] = {};
  bool state2[GRID_SZ][GRID_SZ] = {};

  state1[1][2] = true;
  state1[2][3] = true;
  state1[3][1] = true;
  state1[3][2] = true;
  state1[3][3] = true;

  bool (*curr)[GRID_SZ] = state1;
  bool (*next)[GRID_SZ] = state2;
  double update_total_ms = 0.0;
  for(int i=0; i<TIME_MAX; i++){
    if (UPDATE_IN_PLACE) printf("\033[2J\033[H");

    print_state(curr);
    update(curr, next, update_total_ms);

    bool (*temp)[GRID_SZ] = curr;
    curr = next;
    next = temp;

    if (UPDATE_IN_PLACE) fflush(stdout);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }

  printf("Average update_tmr: %.3f ms\n", update_total_ms / TIME_MAX);
}
