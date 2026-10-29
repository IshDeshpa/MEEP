#pragma once

#include <string>

namespace meep {
  // Host-side check only: this does not wait for asynchronous GPU work.
  void cuda_check(int status, std::string call, std::string file, int line);
}

// Evaluate call once and retain the caller's source location.
#define CUDA_CHECK(call) ::meep::cuda_check((call), #call, __FILE__, __LINE__)
