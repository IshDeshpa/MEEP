NVCC ?= nvcc
CXX ?= g++
CUDA_ARCH ?= 75
NVCCFLAGS ?= -O3 -std=c++17 -lineinfo
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wpedantic
TARGET := build/thread_count

.PHONY: all run test plot world-sweep plot-worlds clean
all: $(TARGET)

build:
	mkdir -p build

$(TARGET): thread_count.cu benchmark_common.h | build
	$(NVCC) $(NVCCFLAGS) -gencode arch=compute_$(CUDA_ARCH),code=sm_$(CUDA_ARCH) -gencode arch=compute_$(CUDA_ARCH),code=compute_$(CUDA_ARCH) $< -o $@

build/test_common: test_common.cpp benchmark_common.h | build
	$(CXX) $(CXXFLAGS) $< -o $@

test: build/test_common
	./build/test_common
	python3 -m py_compile plot.py sweep_worlds.py profile_cache.py

run: $(TARGET)
	./$(TARGET) --output results/latest.csv

plot:
	python3 plot.py results/latest.csv --output results/latest.png

world-sweep: $(TARGET)
	python3 sweep_worlds.py

plot-worlds:
	python3 plot.py results/world_sizes_8/measurements.csv --compare-worlds --output results/world_sizes_8/tick_times.png

clean:
	rm -rf build __pycache__
