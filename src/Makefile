CXX      := g++
GLM_DIR  := glm
OBJ_DIR  := build
# subfolders whose sources are compiled in; includes are relative to src/
# (e.g. #include "render/camera.h")
MODULES  := render state 
MODULES  := $(foreach m,$(MODULES),$(if $(wildcard $(m)),$(m)))
CXXFLAGS := --std=c++20 -O3 -Wall -Wextra -I$(GLM_DIR) -I.
TARGET   := main

# GPU backend: `make BACKEND=cuda` (default, native CUDA) or `make BACKEND=zluda`.
# ZLUDA defaults to the manual CUDA 12.9 install with GCC 14, and puts its
# libcuda replacement ahead of the system one on LD_LIBRARY_PATH at runtime.
BACKEND   ?= cuda
ZLUDA_DIR ?= /mnt/Work/zluda

ifeq ($(BACKEND),zluda)
CUDA_HOME ?= /mnt/Work/cuda-12.9
CUDA_HOST_CXX ?= /usr/bin/g++-14
endif

# CUDA install root, detected in order: the nvcc already on PATH (symlinks
# resolved), then the usual locations. Override with `make CUDA_HOME=/path`.
CUDA_NVCC_PATH := $(shell readlink -f "$$(command -v nvcc)" 2>/dev/null)
CUDA_HOME ?= $(or $(if $(CUDA_NVCC_PATH),$(patsubst %/bin/nvcc,%,$(CUDA_NVCC_PATH))),\
  $(patsubst %/bin/nvcc,%,$(firstword $(wildcard /usr/local/cuda/bin/nvcc /opt/cuda/bin/nvcc \
  $(sort /usr/local/cuda-*/bin/nvcc)))))
# Call nvcc by full path so it finds its helpers (cudafe++, ptxas) even when
# its bin/ is not on PATH; falls back to plain `nvcc` if nothing was found.
NVCC      := $(if $(CUDA_HOME),$(CUDA_HOME)/bin/nvcc,nvcc)$(if $(CUDA_HOST_CXX), -ccbin $(CUDA_HOST_CXX))
CUDA_ARCH ?= sm_75
# CUDA headers (cuda_runtime.h): override with `make CUDA_INC=/path/to/include`.
# Defaults to the first existing include dir under CUDA_HOME.
CUDA_INC  ?= $(patsubst %/cuda_runtime.h,%,$(firstword $(wildcard \
  $(CUDA_HOME)/include/cuda_runtime.h $(CUDA_HOME)/targets/x86_64-linux/include/cuda_runtime.h)))
NVCCFLAGS := --std=c++20 -O3 -arch=$(CUDA_ARCH) -Xcompiler -Wall,-Wextra -I$(GLM_DIR) -I.$(if $(CUDA_INC), -I$(CUDA_INC))

ifeq ($(BACKEND),zluda)
RUN_ENV := env LD_LIBRARY_PATH=$(ZLUDA_DIR)$(if $(LD_LIBRARY_PATH),:$(LD_LIBRARY_PATH))
else ifeq ($(BACKEND),cuda)
RUN_ENV :=
else
$(error BACKEND must be 'cuda' or 'zluda', got '$(BACKEND)')
endif

TEST_DIR := tests

# let make locate sources/headers by bare name in any module
vpath %.cpp $(MODULES) $(TEST_DIR)
vpath %.cu  $(MODULES) $(TEST_DIR)
vpath %.h   $(MODULES)
vpath %.hpp $(MODULES)
SRCS     := $(wildcard *.cpp $(MODULES:%=%/*.cpp))
CU_SRCS  := $(wildcard *.cu $(MODULES:%=%/*.cu))
OBJS     := $(SRCS:%.cpp=$(OBJ_DIR)/%.o) $(CU_SRCS:%.cu=$(OBJ_DIR)/%.o)
# nvcc drives the link when any CUDA sources are present (pulls in cudart)
LINK     := $(if $(CU_SRCS),$(NVCC) $(NVCCFLAGS),$(CXX) $(CXXFLAGS))
# everything except main.cpp, shared by tests
LIB_OBJS := $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
TEST_SRCS  := $(wildcard $(TEST_DIR)/*.cpp)
TEST_NAMES := $(basename $(notdir $(TEST_SRCS)))
TEST_OBJS  := $(TEST_SRCS:%.cpp=$(OBJ_DIR)/%.o)
DEPS     := $(OBJS:.o=.d) $(TEST_OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LINK) -o $@ $^

# -MMD and -MP track header deps
$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(OBJ_DIR)/%.o: %.cu
	@mkdir -p $(dir $@)
	$(NVCC) $(NVCCFLAGS) -MMD -MP -c -o $@ $<

# `make <test-name>` builds tests/<test-name>.cpp into build/tests/<test-name>
# (linked against all non-main sources); `make run-<test-name>` also runs it.
$(OBJ_DIR)/$(TEST_DIR)/%: $(OBJ_DIR)/$(TEST_DIR)/%.o $(LIB_OBJS)
	$(LINK) -o $@ $^

$(TEST_NAMES): %: $(OBJ_DIR)/$(TEST_DIR)/%

$(TEST_NAMES:%=run-%): run-%: %
	$(RUN_ENV) ./$(OBJ_DIR)/$(TEST_DIR)/$*

run: $(TARGET)
	$(RUN_ENV) ./$(TARGET)

# rename parameters/locals to match .clang-tidy
# one clang-tidy job per source file, so `make -jN format` runs N of them at once
HDRS := $(patsubst ./%,%,$(wildcard $(foreach d,. $(MODULES) $(TEST_DIR),$(foreach e,h hpp,$(d)/*.$(e)))))
FORMAT_TARGETS := $(SRCS:%=format/%) $(TEST_SRCS:%=format/%) $(HDRS:%=format/%)

format: $(FORMAT_TARGETS)

$(FORMAT_TARGETS): format/%:
	clang-tidy --fix --fix-errors $* -- -x c++ $(CXXFLAGS)

help:
	@echo "Targets:"
	@echo "  all                 build $(TARGET) (default)"
	@echo "  run                 build and run $(TARGET)"
	@echo "  <test-name>         build tests/<test-name>.cpp into $(OBJ_DIR)/$(TEST_DIR)/<test-name>"
	@echo "  run-<test-name>     build and run that test"
	@echo "  format              run clang-tidy --fix on all sources and headers"
	@echo "  clean               remove $(OBJ_DIR)/ and $(TARGET)"
	@echo "  help                show this message"
	@echo
	@echo "Tests found: $(if $(TEST_NAMES),$(TEST_NAMES),(none in $(TEST_DIR)/))"
	@echo "Modules:     $(MODULES)"
	@echo
	@echo "Variables (override as make VAR=value):"
	@echo "  BACKEND=$(BACKEND)        cuda | zluda (zluda runs binaries with LD_LIBRARY_PATH=$(ZLUDA_DIR))"
	@echo "  ZLUDA_DIR=$(ZLUDA_DIR)"
	@echo "  CUDA_HOME=$(CUDA_HOME)"
	@echo "  CUDA_HOST_CXX=$(CUDA_HOST_CXX) (empty uses nvcc's default host compiler)"
	@echo "  CUDA_INC=$(CUDA_INC)"
	@echo "  CUDA_ARCH=$(CUDA_ARCH)"
	@echo "  NVCC=$(NVCC)"

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

-include $(DEPS)

.SECONDARY:

.PHONY: all run $(TEST_NAMES) $(TEST_NAMES:%=run-%) format $(FORMAT_TARGETS) help clean
