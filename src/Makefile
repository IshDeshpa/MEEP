CXX      := g++
GLM_DIR  := glm
OBJ_DIR  := build
PYTHON   ?= python3
# Shared by normal/debug builds; this is generated data, not an object file.
ALPHA_LUT := build/lut_alpha.bin
# subfolders whose sources are compiled in; includes are relative to src/
# (e.g. #include "render/camera.h")
MODULES  := render state extra window tick
MODULES  := $(foreach m,$(MODULES),$(if $(wildcard $(m)),$(m)))

# Debug flag table. Add flags to a row; higher rows explicitly inherit lower
# rows. Add another DEBUG_FLAGS_<level> row to introduce a level; validation,
# compiler definitions, build directories, and help pick it up automatically.
DEBUG_FLAGS_0 :=
DEBUG_FLAGS_1 := MEEP_DEBUG
DEBUG_FLAGS_2 := $(DEBUG_FLAGS_1) MEEP_DEBUG_RENDER_TRACE

DEBUG    ?= 0
XPRA_MOUSE ?= 0
ifeq ($(filter $(XPRA_MOUSE),0 1),)
$(error XPRA_MOUSE must be 0 or 1)
endif
DEBUG_LEVELS := $(sort $(patsubst DEBUG_FLAGS_%,%,$(filter DEBUG_FLAGS_%,$(.VARIABLES))))
ifeq ($(filter $(DEBUG),$(DEBUG_LEVELS)),)
$(error DEBUG must be one of [$(DEBUG_LEVELS)], got '$(DEBUG)')
endif
DEBUG_FLAGS := $(DEBUG_FLAGS_$(DEBUG))

DEFINES  := -DGLM_ENABLE_EXPERIMENTAL -DMEEP_SRC_DIR='"$(CURDIR)"'
# tick.hpp uses #ifdef, so MEEP_DEBUG must be absent at DEBUG=0.
DEFINES += $(addprefix -D,$(addsuffix =1,$(DEBUG_FLAGS)))
ifeq ($(XPRA_MOUSE),1)
DEFINES += -DMEEP_XPRA_MOUSE=1
endif

CXXFLAGS := --std=c++20 -O3 -Wall -Wextra $(DEFINES) -I$(GLM_DIR) -I.
# libraries go after the objects on the link line, so they are kept by the linker
LDLIBS   := -lX11 -lXi
TARGET   := main
ARGS     ?=
SANITIZER ?=
RUNNER    ?=
VALGRIND  ?= valgrind
VALGRIND_FLAGS ?= --tool=memcheck --leak-check=full --track-origins=yes
GDB       ?= gdb
GDB_FLAGS ?= --args
COMPUTE_SANITIZER ?= compute-sanitizer
COMPUTE_SANITIZER_FLAGS ?= --tool memcheck

ifneq ($(DEBUG),0)
OBJ_DIR := build/debug-$(DEBUG)
CXXFLAGS := $(filter-out -O3,$(CXXFLAGS)) -O0 -g
endif

# Keep sanitizer objects separate so changing SANITIZER never reuses stale objects.
ifneq ($(SANITIZER),)
OBJ_DIR := build/$(SANITIZER)$(if $(filter-out 0,$(DEBUG)),/debug-$(DEBUG))
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
OBJ_DIR := $(OBJ_DIR)/zluda
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
ifneq ($(CUDA_INC),)
CXXFLAGS += -I$(CUDA_INC)
endif
# Device linking supports out-of-line helpers; LTO enables cross-file inlining.
NVCCFLAGS := --std=c++20 --expt-relaxed-constexpr -O3 -rdc=true -dlto -arch=$(CUDA_ARCH) -Xcompiler -Wall,-Wextra $(DEFINES) -I$(GLM_DIR) -I.$(if $(CUDA_INC), -I$(CUDA_INC))
ifneq ($(DEBUG),0)
# -G (device debug) cannot be combined with -dlto, so LTO is dropped here
NVCCFLAGS := $(filter-out -O3 -dlto,$(NVCCFLAGS)) -O0 -g -G
endif
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
else ifeq ($(RUNNER),compute-sanitizer)
RUN_COMMAND := $(COMPUTE_SANITIZER) $(COMPUTE_SANITIZER_FLAGS)
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
$(error RUNNER must be empty, 'valgrind', 'compute-sanitizer', 'gdb', 'address', or 'thread')
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

# All modes publish the same main executable, so relink when switching modes
# even when that mode's existing objects are older than the current executable.
$(TARGET): $(OBJS) build/main.mode | $(ALPHA_LUT)
	$(LINK) -o $@ $(OBJS) $(LDLIBS)

build/main.mode: force-main-mode
	@mkdir -p $(dir $@)
	@printf '%s\n' 'BACKEND=$(BACKEND) DEBUG=$(DEBUG) SANITIZER=$(SANITIZER) FLAGS=$(DEBUG_FLAGS)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

force-main-mode:

# Rebuild when compiler settings change, including the CUDA toolkit used for device LTO.
$(OBJ_DIR)/debug.flags: force-main-mode
	@mkdir -p $(dir $@)
	@printf '%s\n' '$(DEBUG_FLAGS)' '$(CXX) $(CXXFLAGS)' '$(NVCC) $(NVCCFLAGS)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

$(OBJS) $(TEST_CPP_OBJS) $(TEST_CU_OBJS): $(OBJ_DIR)/debug.flags

lut: $(ALPHA_LUT)

alpha-lut: $(ALPHA_LUT)

$(ALPHA_LUT): state/lut_gen_alpha.py
	@mkdir -p $(dir $@)
	$(PYTHON) $< $@

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
	$(TEST_LINK) -o $@ $^ $(LDLIBS)

$(TEST_CU_TARGETS): $(OBJ_DIR)/$(TEST_DIR)/%: $(OBJ_DIR)/$(TEST_DIR)/%.cu.o $(LIB_OBJS)
	$(TEST_LINK) -o $@ $^ $(LDLIBS)

$(OBJ_DIR)/$(TEST_DIR)/world_blank: | $(ALPHA_LUT)

$(TEST_NAMES): %: $(OBJ_DIR)/$(TEST_DIR)/%

$(TEST_NAMES:%=run-%): run-%: % | $(ALPHA_LUT)
	$(RUN_ENV) $(RUN_COMMAND) ./$(OBJ_DIR)/$(TEST_DIR)/$* $(if $(filter world_blank,$*),$(if $(strip $(ARGS)),$(ARGS),"$(abspath $(ALPHA_LUT))"),$(ARGS))

run: $(TARGET) | $(ALPHA_LUT)
	$(RUN_ENV) $(RUN_COMMAND) ./$(TARGET) --lut "$(abspath $(ALPHA_LUT))" $(ARGS)

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
	@echo "  run                 build and run $(TARGET) with the alpha LUT (ARGS can override --lut)"
	@echo "  lut                 generate/update the alpha LUT"
	@echo "  alpha-lut           generate/update $(ALPHA_LUT) from the alpha rules (no CUDA needed)"
	@echo "  <test-name>         build tests/<test-name>.cpp into $(OBJ_DIR)/$(TEST_DIR)/<test-name>"
	@echo "  run-<test-name>     build and run that test"
	@echo "  RUNNER=valgrind     run any run* target under Valgrind Memcheck"
	@echo "  RUNNER=compute-sanitizer  run any run* target under NVIDIA Compute Sanitizer (memcheck by default)"
	@echo "  RUNNER=gdb          launch any run* target in GDB"
	@echo "  DEBUG=<level>       select flags from the table below; nonzero levels use -O0 -g and CUDA -G"
	@echo "  SANITIZER=address   build with AddressSanitizer (separate build/address/)"
	@echo "  SANITIZER=thread    build with ThreadSanitizer (separate build/thread/)"
	@echo "  RUNNER=address|thread   run directly with the matching sanitizer build"
	@echo "  VALGRIND_FLAGS=...      override Valgrind options"
	@echo "  COMPUTE_SANITIZER_FLAGS=...  override Compute Sanitizer options (default: --tool memcheck)"
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
	@echo "  RUNNER=$(RUNNER)  valgrind | compute-sanitizer | gdb | address | thread"
	@echo "  DEBUG=$(DEBUG)            available levels: $(DEBUG_LEVELS)"
	@echo "  XPRA_MOUSE=$(XPRA_MOUSE)       convert Xpra XTEST pointer positions to deltas (0 or 1)"
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
	@echo
	@echo "Debug levels (edit DEBUG_FLAGS_<level> at the top of this Makefile):"
	@printf '  %s\n' $(foreach level,$(DEBUG_LEVELS),'DEBUG=$(level): $(if $(strip $(DEBUG_FLAGS_$(level))),$(DEBUG_FLAGS_$(level)),(no debug flags))')
	@echo "Selected: DEBUG=$(DEBUG), flags=$(if $(strip $(DEBUG_FLAGS)),$(DEBUG_FLAGS),(none)), objects=$(OBJ_DIR)"

clean:
	rm -rf build $(TARGET)

-include $(DEPS)

# Preserve compiled intermediates, but let missing runtime data be regenerated.
.SECONDARY: $(OBJS) $(TEST_CPP_OBJS) $(TEST_CU_OBJS) $(TEST_CPP_TARGETS) $(TEST_CU_TARGETS)

.PHONY: all run lut alpha-lut force-main-mode $(TEST_NAMES) $(TEST_NAMES:%=run-%) format $(FORMAT_TARGETS) help clean
