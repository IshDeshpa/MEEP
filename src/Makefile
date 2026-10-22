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
# Both compile with nvcc; ZLUDA only changes how binaries are run, by putting
# its libcuda replacement ahead of the system one on LD_LIBRARY_PATH.
BACKEND   ?= cuda
ZLUDA_DIR ?= /mnt/Work/zluda
NVCC      := nvcc
CUDA_ARCH ?= compute_75
NVCCFLAGS := --std=c++20 -O3 -arch=$(CUDA_ARCH) -Xcompiler -Wall,-Wextra -I$(GLM_DIR) -I.
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

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

-include $(DEPS)

.SECONDARY:

.PHONY: all run $(TEST_NAMES) $(TEST_NAMES:%=run-%) format $(FORMAT_TARGETS) clean
