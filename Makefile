# Dependency-free build (no CMake needed). The SDL2 window backend is enabled with SDL=1
# (needs SDL2 dev headers + pkg-config); otherwise the headless software backend is used.
#
# Windows (MSYS2 UCRT64 / MinGW): works the same way -- `make SDL=1` -- and produces build/*.exe.
CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Iinclude -Iexamples
SRC      := src/image.cpp src/font.cpp src/renderer.cpp src/software_renderer.cpp src/camera.cpp \
            src/physics.cpp src/tilemap.cpp src/particles.cpp src/animation.cpp src/engine.cpp
LDLIBS   :=
BUILD    := build
EXE      :=
# Native-Windows view of this directory, for the test-data path baked into prism_tests: under MSYS2
# $(CURDIR) is /d/Prism, which a native .exe cannot open (`pwd -W` gives D:/Prism; elsewhere it fails
# and we fall back to plain pwd).
ROOT     := $(shell pwd -W 2>/dev/null || pwd)

ifeq ($(OS),Windows_NT)
  EXE      := .exe
  # run without the MSYS2 shell's DLLs: libgcc/libstdc++ are linked statically
  LDLIBS   += -static-libgcc -static-libstdc++
endif

# Prism games use a plain main(). On MinGW, pkg-config's sdl2 flags also contain -Dmain=SDL_main
# (renames main to a C++-mangled symbol), -lmingw32 -lSDL2main (whose WinMain wants a C SDL_main)
# and -mwindows; together those cause "undefined reference to `SDL_main'". Drop them.
ifeq ($(SDL),1)
  SRC      += src/sdl_renderer.cpp
  CXXFLAGS += -DPRISM_HAS_SDL=1 -DSDL_MAIN_HANDLED $(filter-out -Dmain=SDL_main,$(shell pkg-config --cflags sdl2))
  LDLIBS   := $(filter-out -lmingw32 -lSDL2main -mwindows,$(shell pkg-config --libs sdl2)) $(LDLIBS)
endif

OBJ := $(SRC:%.cpp=$(BUILD)/%.o)

all: $(BUILD)/prism_demo$(EXE) $(BUILD)/prism_arena$(EXE) $(BUILD)/prism_hello$(EXE) $(BUILD)/prism_tests$(EXE)

$(BUILD)/%.o: %.cpp $(wildcard include/prism/*.hpp)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/libprism.a: $(OBJ)
	$(AR) rcs $@ $^

HARNESS := $(wildcard examples/common/*.hpp)
ARENA   := $(wildcard examples/arena/*.hpp)

$(BUILD)/prism_demo$(EXE): examples/platformer/main.cpp $(HARNESS) $(BUILD)/libprism.a
	$(CXX) $(CXXFLAGS) $< $(BUILD)/libprism.a $(LDLIBS) -o $@

$(BUILD)/prism_arena$(EXE): examples/arena/main.cpp $(ARENA) $(HARNESS) $(BUILD)/libprism.a
	$(CXX) $(CXXFLAGS) $< $(BUILD)/libprism.a $(LDLIBS) -o $@

$(BUILD)/prism_hello$(EXE): examples/hello/main.cpp $(BUILD)/libprism.a
	$(CXX) $(CXXFLAGS) $< $(BUILD)/libprism.a $(LDLIBS) -o $@

$(BUILD)/prism_tests$(EXE): tests/test_main.cpp $(ARENA) $(HARNESS) $(BUILD)/libprism.a
	$(CXX) $(CXXFLAGS) -DPRISM_TEST_DATA_DIR='"$(ROOT)/tests/data"' $< $(BUILD)/libprism.a $(LDLIBS) -o $@

test: $(BUILD)/prism_tests$(EXE)
	./$(BUILD)/prism_tests$(EXE)

# End-to-end: a bot plays each game headlessly through the real physics, and every run is
# repeated from scratch to prove it is deterministic (--verify-determinism).
smoke: $(BUILD)/prism_demo$(EXE) $(BUILD)/prism_arena$(EXE)
	./$(BUILD)/prism_demo$(EXE)  --headless --autopilot --size 640x360 --frames 1500 --expect-coins 20 --verify-determinism
	./$(BUILD)/prism_arena$(EXE) --headless --autopilot --size 640x360 --frames 3600 --expect-wave 4 --expect-score 800 --verify-determinism
	./$(BUILD)/prism_arena$(EXE) --headless --autopilot --size 640x360 --frames 2400 --no-title --map examples/arena/maps/crossroads.txt --expect-wave 3
	@./$(BUILD)/prism_arena$(EXE) --headless --map tests/data/bad_map.txt 2>/dev/null; rc=$$?; \
	  if [ $$rc -eq 64 ]; then echo "malformed map refused with status 64: ok"; else echo "malformed map: expected status 64, got $$rc"; exit 1; fi

check: test smoke

clean:
	rm -rf $(BUILD)

.PHONY: all test smoke check clean
