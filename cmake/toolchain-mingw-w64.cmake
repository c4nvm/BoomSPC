# Cross-compiling for Windows from Linux with mingw-w64.
# Point SDL2 and zlib at their mingw builds, e.g.
#   cmake -S . -B winbuild -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake \
#         -DCMAKE_PREFIX_PATH="/path/SDL2-2.30.11/x86_64-w64-mingw32;/path/zlib-mingw" -DCMAKE_BUILD_TYPE=Release
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
list(APPEND CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32/sys-root/mingw ${CMAKE_PREFIX_PATH})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
