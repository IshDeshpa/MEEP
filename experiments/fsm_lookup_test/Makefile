NVCC ?= nvcc
NVCCFLAGS ?= -O3 -std=c++17 -lineinfo -Xptxas=-v
CXX ?= g++
fsm_lookup_bench: fsm_lookup_bench.cu fsm_helpers.h
	$(NVCC) $(NVCCFLAGS) fsm_lookup_bench.cu -o fsm_lookup_bench
hostcheck: host_checks.cpp fsm_helpers.h
	$(CXX) -std=c++17 -O2 -Wall -Wextra host_checks.cpp -o host_checks
	./host_checks
clean:
	rm -f fsm_lookup_bench host_checks
.PHONY: hostcheck clean
