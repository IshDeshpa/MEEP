// multithread live calc, with threads = core count
// no more oversubscription hopefully
// average of 1.8ms update_tmr
// mutex still blocks, also maybe cache contention?
#include <cstdio>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

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

using state_grid = bool (*)[GRID_SZ];
using adj_grid = int (*)[GRID_SZ];

class UpdateWorkers{
public:
  UpdateWorkers(){
    unsigned int core_count = std::thread::hardware_concurrency();
    if (core_count == 0) core_count = 1;

    workers_.reserve(core_count);
    for (unsigned int i = 0; i < core_count; i++){
      workers_.emplace_back(&UpdateWorkers::worker_loop, this);
    }
  }

  ~UpdateWorkers(){
    {
      std::lock_guard<std::mutex> lock(work_mutex_);
      stopping_ = true;
    }
    work_available_.notify_all();

    for (std::thread& worker : workers_){
      worker.join();
    }
  }

  void run(state_grid prev_state, state_grid new_state, adj_grid adj){
    {
      std::lock_guard<std::mutex> lock(work_mutex_);
      prev_state_ = prev_state;
      new_state_ = new_state;
      adj_ = adj;
      next_cell_ = 0;
      completed_ = 0;
      generation_++;
    }
    work_available_.notify_all();

    std::unique_lock<std::mutex> lock(work_mutex_);
    work_complete_.wait(lock, [this](){
      return completed_ == workers_.size();
    });
  }

private:
  void worker_loop(){
    unsigned int completed_generation = 0;

    while (true){
      state_grid prev_state;
      state_grid new_state;
      adj_grid adj;

      {
        std::unique_lock<std::mutex> lock(work_mutex_);
        work_available_.wait(lock, [this, &completed_generation](){
          return stopping_ || generation_ > completed_generation;
        });

        if (stopping_) return;

        completed_generation = generation_;
        prev_state = prev_state_;
        new_state = new_state_;
        adj = adj_;
      }

      int cell_index;
      while ((cell_index = next_cell_.fetch_add(1)) < GRID_SZ * GRID_SZ){
        update_cell(prev_state, new_state, adj,
                    cell_index / GRID_SZ, cell_index % GRID_SZ);
      }

      {
        std::lock_guard<std::mutex> lock(work_mutex_);
        completed_++;
        if (completed_ == workers_.size()){
          work_complete_.notify_one();
        }
      }
    }
  }

  std::vector<std::thread> workers_;
  std::mutex work_mutex_;
  std::condition_variable work_available_;
  std::condition_variable work_complete_;
  bool stopping_ = false;
  unsigned int generation_ = 0;
  unsigned int completed_ = 0;
  std::atomic<int> next_cell_ = 0;
  state_grid prev_state_ = nullptr;
  state_grid new_state_ = nullptr;
  adj_grid adj_ = nullptr;
};

void update(bool prev_state[GRID_SZ][GRID_SZ], bool new_state[GRID_SZ][GRID_SZ],
            UpdateWorkers& workers, double& update_total_ms){
  ScopedTimer tmr("update_tmr");

  int adj[GRID_SZ][GRID_SZ];
  generate_adj(prev_state, adj);
  workers.run(prev_state, new_state, adj);

  update_total_ms += tmr.elapsed_ms();
}

int main(){
  bool state1[GRID_SZ][GRID_SZ] = {};
  bool state2[GRID_SZ][GRID_SZ] = {};
  UpdateWorkers workers;

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
    update(curr, next, workers, update_total_ms);

    bool (*temp)[GRID_SZ] = curr;
    curr = next;
    next = temp;

    if (UPDATE_IN_PLACE) fflush(stdout);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }

  printf("Average update_tmr: %.3f ms\n", update_total_ms / TIME_MAX);
}
