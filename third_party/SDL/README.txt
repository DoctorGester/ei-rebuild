SDL3 (https://libsdl.org, https://github.com/libsdl-org/SDL), zlib licence (LICENSE.txt).
SDL 3.4.18 (tag release-3.4.18, commit 829a65d769d935c4852f8159e964312c0957260a) from its source release
SDL3-3.4.18.tar.gz: CMakeLists.txt, cmake/, include/, src/, wayland-protocols/, LICENSE.txt and .git-hash;
its tests, examples, documents and IDE projects left out. Built statically into ei-rebuild.exe (CMakeLists.txt).
Used by the platform layer (src/platform/sdl, src/audio/audio.cpp; docs/notes/platform.md).

Local changes from that release:
- src/video/cocoa/SDL_cocoamouse.m and SDL_cocoawindow.m honor SDL_HINT_MOUSE_RELATIVE_CURSOR_VISIBLE: keep the
  selected cursor visible without warping it to the window centre on capture. The game holds its rotation cursor
  at the right-button press point while reading relative motion.
