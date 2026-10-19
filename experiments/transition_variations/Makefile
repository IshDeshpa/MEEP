NVCC ?= nvcc
CXX ?= g++
CUDA_ARCH ?= 75
NVCCFLAGS ?= -O3 -std=c++17 -lineinfo -Xcompiler=-pthread
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wpedantic -pthread
TARGET := build/transition_variations
HEADERS := transitions.h reference_world.h ../thread_count/benchmark_common.h ../fsm_lookup_test/fsm_helpers.h

.PHONY: all run sweep profile test clean
all: $(TARGET)

build:
	mkdir -p build

$(TARGET): transition_variations.cu $(HEADERS) | build
	$(NVCC) $(NVCCFLAGS) -gencode arch=compute_$(CUDA_ARCH),code=sm_$(CUDA_ARCH) -gencode arch=compute_$(CUDA_ARCH),code=compute_$(CUDA_ARCH) $< -o $@

build/test_transitions: test_transitions.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $< -o $@

test: build/test_transitions
	./build/test_transitions
	python3 -m unittest -v test_profile_counters.py test_analysis.py

run: $(TARGET)
	./$(TARGET) --preheat-ms 500 --output results/latest.csv

profile: $(TARGET)
	python3 profile_counters.py results/latest.csv $(PROFILE_FLAGS)

sweep: $(TARGET)
	python3 sweep.py --output-dir results/world_material_sweep $(SWEEP_FLAGS)

clean:
	rm -rf build
