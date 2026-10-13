NVCC ?= nvcc
NVCCFLAGS ?= -O3 -std=c++17 -lineinfo -Xptxas=-v
lookup_memory: lookup_memory.cu lookup_helpers.h
	$(NVCC) $(NVCCFLAGS) lookup_memory.cu -o lookup_memory
clean:
	rm -f lookup_memory
.PHONY: clean
