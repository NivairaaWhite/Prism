# Dependency-free build (no CMake needed). The SDL2 window backend is enabled with SDL=1
# (needs SDL2 dev headers + pkg-config); otherwise the headless software backend is used.
CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Iinclude
SRC      := src/image.cpp src/font.cpp src/renderer.cpp src/software_renderer.cpp src/camera.cpp \
            src/physics.cpp src/tilemap.cpp src/particles.cpp src/animation.cpp src/engine.cpp
LDLIBS   :=
BUILD    := build

ifeq ($(SDL),1)
  SRC      += src/sdl_renderer.cpp
  CXXFLAGS += -DPRISM_HAS_SDL=1 $(shell pkg-config --cflags sdl2)
  LDLIBS   += $(shell pkg-config --libs sdl2)
endif

OBJ := $(SRC:%.cpp=$(BUILD)/%.o)

all: $(BUILD)/prism_demo $(BUILD)/prism_hello $(BUILD)/prism_tests

$(BUILD)/%.o: %.cpp $(wildcard include/prism/*.hpp)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/libprism.a: $(OBJ)
	ar rcs $@ $^

$(BUILD)/prism_demo: examples/platformer/main.cpp $(BUILD)/libprism.a
	$(CXX) $(CXXFLAGS) $< $(BUILD)/libprism.a $(LDLIBS) -o $@

$(BUILD)/prism_hello: examples/hello/main.cpp $(BUILD)/libprism.a
	$(CXX) $(CXXFLAGS) $< $(BUILD)/libprism.a $(LDLIBS) -o $@

$(BUILD)/prism_tests: tests/test_main.cpp $(BUILD)/libprism.a
	$(CXX) $(CXXFLAGS) -DPRISM_TEST_DATA_DIR='"$(CURDIR)/tests/data"' $< $(BUILD)/libprism.a $(LDLIBS) -o $@

test: $(BUILD)/prism_tests
	./$(BUILD)/prism_tests

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
