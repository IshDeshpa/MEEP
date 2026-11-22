#pragma once

namespace meep {
  // Host-side check only: this does not wait for asynchronous GPU work.
  void cudaCheck(int status, const char* call, const char* file, int line);
}

// Evaluate call once and retain the caller's source location.
#define CUDA_CHECK(call) ::meep::cudaCheck((call), #call, __FILE__, __LINE__)
