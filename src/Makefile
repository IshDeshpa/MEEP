CXX      := g++
GLM_DIR  := glm
OBJ_DIR  := build
# subfolders whose sources are compiled in; includes are relative to src/
# (e.g. #include "render/camera.h")
MODULES  := render state extra
MODULES  := $(foreach m,$(MODULES),$(if $(wildcard $(m)),$(m)))

DEFINES  := -DGLM_ENABLE_EXPERIMENTAL -DMEEP_SRC_DIR='"$(CURDIR)"'

CXXFLAGS := --std=c++20 -O3 -Wall -Wextra $(DEFINES) -I$(GLM_DIR) -I.
TARGET   := main
ARGS     ?=
SANITIZER ?=
RUNNER    ?=
VALGRIND  ?= valgrind
VALGRIND_FLAGS ?= --tool=memcheck --leak-check=full --track-origins=yes
GDB       ?= gdb
GDB_FLAGS ?= --args

# Keep sanitizer objects separate so changing SANITIZER never reuses stale objects.
ifneq ($(SANITIZER),)
OBJ_DIR := build/$(SANITIZER)
CXXFLAGS := $(filter-out -O3,$(CXXFLAGS)) -O1 -g -fno-omit-frame-pointer
endif
ifeq ($(SANITIZER),address)
CXXFLAGS += -fsanitize=address
else ifeq ($(SANITIZER),thread)
CXXFLAGS += -fsanitize=thread
else ifneq ($(SANITIZER),)
$(error SANITIZER must be empty, 'address', or 'thread')
endif

# GPU backend: `make BACKEND=cuda` (default, native CUDA) or `make BACKEND=zluda`.
# ZLUDA defaults to the manual CUDA 12.9 install with GCC 14, and puts its
# libcuda replacement ahead of the system one on LD_LIBRARY_PATH at runtime.
BACKEND   ?= cuda
ZLUDA_DIR ?= /mnt/Work/zluda
ROCM_LIB_DIR ?= /opt/rocm/lib

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
# Device linking supports out-of-line helpers; LTO enables cross-file inlining.
NVCCFLAGS := --std=c++20 --expt-relaxed-constexpr -O3 -rdc=true -dlto -arch=$(CUDA_ARCH) -Xcompiler -Wall,-Wextra $(DEFINES) -I$(GLM_DIR) -I.$(if $(CUDA_INC), -I$(CUDA_INC))
ifneq ($(SANITIZER),)
NVCCFLAGS := $(filter-out -O3,$(NVCCFLAGS)) -O1 -g -Xcompiler=-fno-omit-frame-pointer
endif
ifeq ($(SANITIZER),address)
NVCCFLAGS += -Xcompiler=-fsanitize=address
else ifeq ($(SANITIZER),thread)
NVCCFLAGS += -Xcompiler=-fsanitize=thread
endif

ifeq ($(RUNNER),)
RUN_COMMAND :=
else ifeq ($(RUNNER),valgrind)
RUN_COMMAND := $(VALGRIND) $(VALGRIND_FLAGS)
else ifeq ($(RUNNER),gdb)
RUN_COMMAND := $(GDB) $(GDB_FLAGS)
else ifeq ($(RUNNER),address)
ifneq ($(SANITIZER),address)
$(error RUNNER=address requires SANITIZER=address)
endif
RUN_COMMAND :=
else ifeq ($(RUNNER),thread)
ifneq ($(SANITIZER),thread)
$(error RUNNER=thread requires SANITIZER=thread)
endif
RUN_COMMAND :=
else
$(error RUNNER must be empty, 'valgrind', 'gdb', 'address', or 'thread')
endif

ifeq ($(BACKEND),zluda)
RUN_ENV := env LD_LIBRARY_PATH=$(ZLUDA_DIR):$(ROCM_LIB_DIR)$(if $(LD_LIBRARY_PATH),:$(LD_LIBRARY_PATH))
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
TEST_SRCS    := $(wildcard $(TEST_DIR)/*.cpp)
TEST_CU_SRCS := $(wildcard $(TEST_DIR)/*.cu)
# Keep CUDA objects distinct when a .cpp and .cu share the same basename.
OBJS     := $(SRCS:%.cpp=$(OBJ_DIR)/%.o) $(CU_SRCS:%.cu=$(OBJ_DIR)/%.cu.o)
# nvcc drives the link when any CUDA sources are present (pulls in cudart)
LINK     := $(if $(CU_SRCS),$(NVCC) $(NVCCFLAGS),$(CXX) $(CXXFLAGS))
# everything except main.cpp, shared by tests
LIB_OBJS := $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
TEST_CPP_NAMES := $(basename $(notdir $(TEST_SRCS)))
TEST_CU_NAMES  := $(basename $(notdir $(TEST_CU_SRCS)))
TEST_NAMES := $(TEST_CPP_NAMES) $(TEST_CU_NAMES)
TEST_CPP_OBJS := $(TEST_SRCS:%.cpp=$(OBJ_DIR)/%.o)
TEST_CU_OBJS  := $(TEST_CU_SRCS:%.cu=$(OBJ_DIR)/%.cu.o)
TEST_CPP_TARGETS := $(TEST_CPP_NAMES:%=$(OBJ_DIR)/$(TEST_DIR)/%)
TEST_CU_TARGETS  := $(TEST_CU_NAMES:%=$(OBJ_DIR)/$(TEST_DIR)/%)
TEST_LINK := $(if $(or $(CU_SRCS),$(TEST_CU_SRCS)),$(NVCC) $(NVCCFLAGS),$(CXX) $(CXXFLAGS))
DEPS     := $(OBJS:.o=.d) $(TEST_CPP_OBJS:.o=.d) $(TEST_CU_OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LINK) -o $@ $^

# -MMD and -MP track header deps
$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(OBJ_DIR)/%.cu.o: %.cu
	@mkdir -p $(dir $@)
	$(NVCC) $(NVCCFLAGS) -MMD -MP -c -o $@ $<

# `make <test-name>` builds a test source into build/tests/<test-name>
# (linked against all non-main sources); `make run-<test-name>` also runs it.
$(TEST_CPP_TARGETS): $(OBJ_DIR)/$(TEST_DIR)/%: $(OBJ_DIR)/$(TEST_DIR)/%.o $(LIB_OBJS)
	$(TEST_LINK) -o $@ $^

$(TEST_CU_TARGETS): $(OBJ_DIR)/$(TEST_DIR)/%: $(OBJ_DIR)/$(TEST_DIR)/%.cu.o $(LIB_OBJS)
	$(TEST_LINK) -o $@ $^

$(TEST_NAMES): %: $(OBJ_DIR)/$(TEST_DIR)/%

$(TEST_NAMES:%=run-%): run-%: %
	$(RUN_ENV) $(RUN_COMMAND) ./$(OBJ_DIR)/$(TEST_DIR)/$* $(ARGS)

run: $(TARGET)
	$(RUN_ENV) $(RUN_COMMAND) ./$(TARGET) $(ARGS)

# rename parameters/locals to match .clang-tidy
# one clang-tidy job per source file, so `make -jN format` runs N of them at once
HDRS := $(patsubst ./%,%,$(wildcard $(foreach d,. $(MODULES) $(TEST_DIR),$(foreach e,h hpp,$(d)/*.$(e)))))
FORMAT_CPP_TARGETS := $(SRCS:%=format/%) $(TEST_SRCS:%=format/%) $(HDRS:%=format/%)
FORMAT_CU_TARGETS := $(CU_SRCS:%=format/%) $(TEST_CU_SRCS:%=format/%)
FORMAT_TARGETS := $(FORMAT_CPP_TARGETS) $(FORMAT_CU_TARGETS)

format: $(FORMAT_TARGETS)

$(FORMAT_CPP_TARGETS): format/%:
	clang-tidy --fix --fix-errors $* -- -x c++ $(CXXFLAGS)

$(FORMAT_CU_TARGETS): format/%.cu: %.cu
	clang-tidy --fix --fix-errors $< -- -x cuda $(CXXFLAGS) $(if $(CUDA_INC),-I$(CUDA_INC))

help:
	@echo "Targets:"
	@echo "  all                 build $(TARGET) (default)"
	@echo "  run                 build and run $(TARGET)"
	@echo "  <test-name>         build tests/<test-name>.cpp into $(OBJ_DIR)/$(TEST_DIR)/<test-name>"
	@echo "  run-<test-name>     build and run that test"
	@echo "  RUNNER=valgrind     run any run* target under Valgrind Memcheck"
	@echo "  RUNNER=gdb          launch any run* target in GDB"
	@echo "  SANITIZER=address   build with AddressSanitizer (separate build/address/)"
	@echo "  SANITIZER=thread    build with ThreadSanitizer (separate build/thread/)"
	@echo "  RUNNER=address|thread   run directly with the matching sanitizer build"
	@echo "  VALGRIND_FLAGS=...      override Valgrind options"
	@echo "  GDB_FLAGS=...           override GDB options"
	@echo "  format              run clang-tidy --fix on all C++, CUDA, and header files"
	@echo "  clean               remove build/ and $(TARGET)"
	@echo "  help                show this message"
	@echo
	@echo "Tests found: $(if $(TEST_NAMES),$(TEST_NAMES),(none in $(TEST_DIR)/))"
	@echo "Modules:     $(MODULES)"
	@echo
	@echo "Variables (override as make VAR=value):"
	@echo '  ARGS="..."          arguments passed to run and run-<test-name>'
	@echo "  RUNNER=$(RUNNER)  valgrind | gdb | address | thread"
	@echo "  SANITIZER=$(SANITIZER)  address | thread"
	@echo "  VALGRIND_FLAGS=$(VALGRIND_FLAGS)"
	@echo "  GDB_FLAGS=$(GDB_FLAGS)"
	@echo "  BACKEND=$(BACKEND)        cuda | zluda (zluda runs binaries with LD_LIBRARY_PATH=$(ZLUDA_DIR))"
	@echo "  ZLUDA_DIR=$(ZLUDA_DIR)"
	@echo "  ROCM_LIB_DIR=$(ROCM_LIB_DIR)"
	@echo "  CUDA_HOME=$(CUDA_HOME)"
	@echo "  CUDA_HOST_CXX=$(CUDA_HOST_CXX) (empty uses nvcc's default host compiler)"
	@echo "  CUDA_INC=$(CUDA_INC)"
	@echo "  CUDA_ARCH=$(CUDA_ARCH)"
	@echo "  NVCC=$(NVCC)"

clean:
	rm -rf build $(TARGET)

-include $(DEPS)

.SECONDARY:

.PHONY: all run $(TEST_NAMES) $(TEST_NAMES:%=run-%) format $(FORMAT_TARGETS) help clean
