cmake_minimum_required(VERSION 3.16)
project(Prism VERSION 0.3.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(PRISM_WITH_SDL      "Build the SDL2 window backend if SDL2 is found" ON)
option(PRISM_BUILD_EXAMPLES "Build the example games (platformer, arena) and hello" ON)
option(PRISM_BUILD_TESTS    "Build the test suite" ON)
option(PRISM_STATIC_RUNTIME "MinGW: link libgcc/libstdc++ statically so the .exe runs outside the MSYS2 shell" ON)
option(PRISM_WINDOWS_GUI    "MinGW: build windowed games without a console window (hides --help / error output)" OFF)

add_library(prism STATIC
  src/image.cpp src/font.cpp src/renderer.cpp src/software_renderer.cpp src/camera.cpp
  src/physics.cpp src/tilemap.cpp src/particles.cpp src/animation.cpp src/engine.cpp)
target_include_directories(prism PUBLIC include)
target_compile_features(prism PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(prism PRIVATE /W4)
else()
  target_compile_options(prism PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Optional SDL2 window backend. Without it Prism still builds and runs headless
# (software renderer), which is what the tests use.
#
# Prism games define a plain main() and never use SDL2main, so the target we link must be *just*
# the SDL2 library. On MinGW, sdl2.pc / sdl2-config ask for much more: -Dmain=SDL_main (renames
# main to a C++-mangled symbol), -lmingw32 -lSDL2main (whose WinMain looks for a C SDL_main) and
# -mwindows. Together those give "undefined reference to `SDL_main'", so they are removed here.
set(PRISM_SDL_FOUND OFF)
set(PRISM_SDL_DLL_HINTS "")
if(PRISM_WITH_SDL)
  # 1) SDL2's own CMake package: SDL2::SDL2 is the bare library (SDL2::SDL2main is separate).
  #    On MinGW it still appends -mwindows (a GUI-subsystem exe with no console), unless told not to.
  if(NOT PRISM_WINDOWS_GUI)
    set(SDL2_NO_MWINDOWS ON)
  endif()
  find_package(SDL2 QUIET CONFIG)
  if(TARGET SDL2::SDL2)
    add_library(prism_sdl2 INTERFACE)
    target_link_libraries(prism_sdl2 INTERFACE SDL2::SDL2)
    set(PRISM_SDL_FOUND ON)
    foreach(_v SDL2_PREFIX SDL2_EXEC_PREFIX)
      if(${_v})
        list(APPEND PRISM_SDL_DLL_HINTS "${${_v}}/bin")
      endif()
    endforeach()
    if(SDL2_LIBDIR)
      list(APPEND PRISM_SDL_DLL_HINTS "${SDL2_LIBDIR}/../bin")
    endif()
  else()
    # 2) pkg-config, with the Windows entry-point machinery filtered out.
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(PRISM_SDL2 QUIET sdl2)
    endif()
    if(PRISM_SDL2_FOUND)
      set(_sdl_libs   ${PRISM_SDL2_LIBRARIES})
      set(_sdl_cflags ${PRISM_SDL2_CFLAGS_OTHER})
      set(_sdl_ldflgs ${PRISM_SDL2_LDFLAGS_OTHER})
      list(FILTER _sdl_libs   EXCLUDE REGEX "^(mingw32|SDL2main|cygwin)$")
      list(FILTER _sdl_cflags EXCLUDE REGEX "^-Dmain=")
      list(FILTER _sdl_ldflgs EXCLUDE REGEX "^-mwindows$")
      add_library(prism_sdl2 INTERFACE)
      target_include_directories(prism_sdl2 INTERFACE ${PRISM_SDL2_INCLUDE_DIRS})
      if(PRISM_SDL2_LIBRARY_DIRS)
        target_link_directories(prism_sdl2 INTERFACE ${PRISM_SDL2_LIBRARY_DIRS})
      endif()
      target_compile_options(prism_sdl2 INTERFACE ${_sdl_cflags})
      target_link_libraries(prism_sdl2 INTERFACE ${_sdl_libs} ${_sdl_ldflgs})
      set(PRISM_SDL_FOUND ON)
      foreach(_d ${PRISM_SDL2_LIBRARY_DIRS})
        list(APPEND PRISM_SDL_DLL_HINTS "${_d}/../bin")
      endforeach()
    endif()
  endif()
  if(PRISM_SDL_FOUND)
    target_sources(prism PRIVATE src/sdl_renderer.cpp)
    target_compile_definitions(prism PUBLIC PRISM_HAS_SDL=1 SDL_MAIN_HANDLED)
    target_link_libraries(prism PUBLIC prism_sdl2)
  endif()
endif()
message(STATUS "Prism: SDL2 window backend: ${PRISM_SDL_FOUND}")

# Windows/MinGW conveniences for every executable we build (no-ops elsewhere).
#  * copy SDL2.dll next to the .exe so a double-click works without the MSYS2 shell's PATH;
#  * link libgcc/libstdc++ statically (PRISM_STATIC_RUNTIME) so no libstdc++-6.dll etc. is needed;
#  * optionally hide the console window (PRISM_WINDOWS_GUI).
unset(PRISM_SDL_DLL)
unset(PRISM_SDL_DLL CACHE)  # search again on every configure
if(WIN32 AND PRISM_SDL_FOUND)
  get_filename_component(_cxx_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)  # MSYS2: SDL2.dll sits beside g++
  find_file(PRISM_SDL_DLL NAMES SDL2.dll HINTS ${PRISM_SDL_DLL_HINTS} ${_cxx_dir} PATH_SUFFIXES bin)
endif()
# MinGW "posix"-threads toolchains (MSYS2's) make libstdc++ pull in libwinpthread-1.dll even for a
# sleep. Link winpthread statically too -- but only if this toolchain can actually do that. The
# trailing -Bdynamic matters: without it every later -lSDL2 would pick the static libSDL2.a.
set(PRISM_RUNTIME_LINK_OPTIONS "")
if(MINGW AND PRISM_STATIC_RUNTIME)
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_LINK_OPTIONS -static-libgcc -static-libstdc++
      -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic)
  check_cxx_source_compiles("#include <chrono>
#include <thread>
int main() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }" PRISM_STATIC_WINPTHREAD_OK)
  unset(CMAKE_REQUIRED_LINK_OPTIONS)
  if(PRISM_STATIC_WINPTHREAD_OK)
    set(PRISM_RUNTIME_LINK_OPTIONS -static-libgcc -static-libstdc++
        -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic)
  else()
    set(PRISM_RUNTIME_LINK_OPTIONS -static-libgcc -static-libstdc++)
  endif()
endif()

function(prism_windows_setup tgt)
  if(NOT WIN32)
    return()
  endif()
  if(MINGW)
    if(PRISM_RUNTIME_LINK_OPTIONS)
      target_link_options(${tgt} PRIVATE ${PRISM_RUNTIME_LINK_OPTIONS})
    endif()
    if(PRISM_WINDOWS_GUI)
      target_link_options(${tgt} PRIVATE -mwindows)
    endif()
  endif()
  if(PRISM_SDL_DLL)
    add_custom_command(TARGET ${tgt} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PRISM_SDL_DLL}" "$<TARGET_FILE_DIR:${tgt}>")
  endif()
endfunction()

if(PRISM_BUILD_EXAMPLES)
  # The games share examples/common/harness.hpp (CLI + headless runner); include dir is "examples".
  add_executable(prism_demo examples/platformer/main.cpp)
  target_link_libraries(prism_demo PRIVATE prism)
  target_include_directories(prism_demo PRIVATE examples)
  add_executable(prism_arena examples/arena/main.cpp)
  target_link_libraries(prism_arena PRIVATE prism)
  target_include_directories(prism_arena PRIVATE examples)
  add_executable(prism_hello examples/hello/main.cpp)
  target_link_libraries(prism_hello PRIVATE prism)
  foreach(_t prism_demo prism_arena prism_hello)
    prism_windows_setup(${_t})
  endforeach()
endif()

if(PRISM_BUILD_TESTS)
  enable_testing()
  add_executable(prism_tests tests/test_main.cpp)
  target_link_libraries(prism_tests PRIVATE prism)
  target_include_directories(prism_tests PRIVATE examples)  # the tests cover harness.hpp and arena_data.hpp
  target_compile_definitions(prism_tests PRIVATE PRISM_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  prism_windows_setup(prism_tests)
  add_test(NAME prism_tests COMMAND prism_tests)
  if(PRISM_BUILD_EXAMPLES)
    # End-to-end smoke tests: a bot plays each game headlessly through the real physics, and
    # --verify-determinism reruns it from scratch and fails unless the final frame is identical.
    add_test(NAME demo_autopilot COMMAND prism_demo --headless --autopilot --size 640x360 --frames 1500 --expect-coins 20 --verify-determinism)
    add_test(NAME arena_autopilot COMMAND prism_arena --headless --autopilot --size 640x360 --frames 3600 --expect-wave 4 --expect-score 800 --verify-determinism)
    add_test(NAME arena_custom_map COMMAND prism_arena --headless --autopilot --size 640x360 --frames 2400 --no-title
             --map ${CMAKE_CURRENT_SOURCE_DIR}/examples/arena/maps/crossroads.txt --expect-wave 3)
    # A malformed map must be refused with the usage-error status (64), not start a broken game.
    add_test(NAME arena_rejects_bad_map COMMAND prism_arena --headless --map ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bad_map.txt)
    set_tests_properties(arena_rejects_bad_map PROPERTIES WILL_FAIL TRUE)
  endif()
endif()
