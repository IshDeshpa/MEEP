CXX      := g++
GLM_DIR  := glm
OBJ_DIR  := build
CXXFLAGS := --std=c++20 -O3 -Wall -Wextra -I$(GLM_DIR) -Irender
TARGET   := main
SRCS     := $(wildcard *.cpp render/*.cpp)
OBJS     := $(SRCS:%.cpp=$(OBJ_DIR)/%.o)
DEPS     := $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# -MMD and -MP track header deps
$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

run: $(TARGET)
	./$(TARGET)

# rename parameters/locals to match .clang-tidy
format:
	clang-tidy --fix --fix-errors $(SRCS) -- $(CXXFLAGS)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

-include $(DEPS)

.PHONY: all run format clean
