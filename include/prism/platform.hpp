#pragma once
// Platform entry-point guard.
//
// Every Prism program uses a plain `int main(int argc, char** argv)` on every platform.
//
// SDL's own recommendation on Windows is different: rename main to SDL_main (the flag
// `-Dmain=SDL_main` is part of `sdl2-config --cflags` and of sdl2.pc on MinGW) and link SDL2main,
// whose WinMain() calls SDL_main(). That only works if SDL_main has C linkage. In C++ the renamed
// function is name-mangled (_Z8SDL_mainiPPc), SDL2main cannot find it, and the link fails with
//
//     undefined reference to `SDL_main'
//
// Prism needs none of this: the build files no longer link SDL2main, and this header makes sure
// a stray -Dmain=SDL_main cannot rename the entry point, nor SDL.h redefine it, whichever build
// system (CMake, make, an IDE, a hand-written command line) compiles the program.
#ifdef main
#undef main
#endif

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED  // "I provide main() myself" (SDL_SetMainReady() is called by SdlRenderer)
#endif
