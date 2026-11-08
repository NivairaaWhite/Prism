#!/usr/bin/env bash
# One-shot Windows build for MSYS2 (UCRT64 / MINGW64 / CLANG64 shell): installs what is missing,
# builds Prism with the SDL2 window backend, runs every check, and tells you how to play.
#
#   cd /d/Prism
#   bash scripts/build_msys2.sh            # build + test
#   bash scripts/build_msys2.sh --clean    # throw the old build/ folder away first
#
# Works on Linux/macOS too (the package step is skipped when pacman is not around).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [ "${1:-}" = "--clean" ]; then
  echo "==> removing old build/ folder"
  rm -rf build
fi

# 1) Dependencies (MSYS2 only): just what the build needs, not the whole 'toolchain' group (gdb, python, tk...). MINGW_PACKAGE_PREFIX is set by the MSYS2 shell, e.g. mingw-w64-ucrt-x86_64.
if command -v pacman >/dev/null 2>&1 && [ -n "${MINGW_PACKAGE_PREFIX:-}" ]; then
  echo "==> checking packages for ${MSYSTEM:-MSYS2} (${MINGW_PACKAGE_PREFIX})"
  pacman -S --needed --noconfirm \
    "${MINGW_PACKAGE_PREFIX}-gcc" "${MINGW_PACKAGE_PREFIX}-cmake" \
    "${MINGW_PACKAGE_PREFIX}-ninja" "${MINGW_PACKAGE_PREFIX}-pkgconf" "${MINGW_PACKAGE_PREFIX}-SDL2"
elif command -v pacman >/dev/null 2>&1; then
  echo "!! You are in the plain MSYS shell. Close it and open 'MSYS2 UCRT64' from the Start menu," >&2
  echo "!! then run this script again." >&2
  exit 1
fi

# 2) Configure + build (Ninja if present, otherwise CMake's default generator)
GEN=()
command -v ninja >/dev/null 2>&1 && GEN=(-G Ninja)
echo "==> configuring"
cmake -B build "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release
echo "==> building"
cmake --build build -j

# 3) Checks: unit tests + a bot playing each game headlessly (with a determinism re-run)
echo "==> running tests"
ctest --test-dir build --output-on-failure

EXE=""
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;; esac
cat <<MSG

==> Done. Play with:
      ./build/prism_demo${EXE}     platformer   (A/D move, Space jump, Esc quit)
      ./build/prism_arena${EXE}    arena shooter (WASD move, mouse aim + click, Space dash)
      ./build/prism_hello${EXE}    smallest example
    The .exe files also run by double-click: SDL2.dll is copied next to them.
MSG
