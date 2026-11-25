#pragma once

#include <cstddef>
#include <chrono>
#include <thread>
#include <string>
#include <cstdio>

#ifdef MEEP_DEBUG
#include <algorithm>
#include <iterator>
#include <mutex>
#endif

using namespace std::chrono;

static constexpr unsigned TICK_HIST = 6000;

// Ticks per second. A zero rate leaves the ticker unpaced.
using TickRate = float;

using Clock = std::chrono::steady_clock;

namespace TickRates {
inline constexpr TickRate ONE_TPS = 1.0F;
inline constexpr TickRate TWO_TPS = 2.0F;
inline constexpr TickRate FIVE_TPS = 5.0F;
inline constexpr TickRate TEN_TPS = 10.0F;
inline constexpr TickRate THIRTY_TPS = 30.0F;
inline constexpr TickRate SIXTY_TPS = 60.0F;
inline constexpr TickRate UNLIMITED = 0.0F;
} // namespace TickRates

class TickSource {
public:
  explicit TickSource(TickRate ticks_per_second) : ticks_per_second(ticks_per_second) {}
  explicit TickSource(TickRate ticks_per_second, std::string name) : name(name), ticks_per_second(ticks_per_second) {}

  void incAndWait(Clock::time_point start) {
    counter++;
    if (ticks_per_second > 0.0F) {
      const auto period = duration_cast<Clock::duration>(
          duration<float>(1.0F / ticks_per_second));
    
      Clock::time_point now = Clock::now();
      if(now < start + period) std::this_thread::sleep_until(start + period);
      else {
        printf("%s tick overrun: %0.2f\n", this->name.c_str(),
                  std::chrono::duration<double>(now - (start + period)).count());
      }
    }
#ifdef MEEP_DEBUG
    {
      std::lock_guard lock(history_mutex);
      recent_tick_times[history_count % TICK_HIST] = Clock::now() - start;
      ++history_count;
    }
#endif
  }

#ifdef MEEP_DEBUG
  double averageTPS(size_t interval) {
    std::lock_guard lock(history_mutex);
    Clock::duration total = Clock::duration::zero();
    interval = std::min(interval, std::min(history_count, std::size(recent_tick_times)));
    if (interval == 0) return 0.0;
    for (size_t i = 0; i < interval; i++) {
      total += recent_tick_times[(history_count - 1 - i) % std::size(recent_tick_times)];
    }
    const double seconds = std::chrono::duration<double>(total).count();
    return seconds > 0.0 ? static_cast<double>(interval) / seconds : 0.0;
  }
#endif

  std::size_t counter = 0;
  std::string name{"anonymous"};

private:
  TickRate ticks_per_second{TickRates::SIXTY_TPS};

#ifdef MEEP_DEBUG
  std::mutex history_mutex;
  std::size_t history_count = 0;
  Clock::duration recent_tick_times[TICK_HIST]{};
#endif
};
