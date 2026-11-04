# Cross-compile the Windows build on Linux with llvm-mingw (https://github.com/mstorsjo/llvm-mingw).
# Usage: cmake -S windows -B windows/build-mingw -DCMAKE_TOOLCHAIN_FILE=windows/cmake/llvm-mingw.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(DEFINED ENV{LLVM_MINGW})
    set(_root "$ENV{LLVM_MINGW}")
else()
    set(_root "$ENV{HOME}/.local/llvm-mingw")
endif()

set(CMAKE_C_COMPILER   "${_root}/bin/x86_64-w64-mingw32-clang")
set(CMAKE_CXX_COMPILER "${_root}/bin/x86_64-w64-mingw32-clang++")
set(CMAKE_RC_COMPILER  "${_root}/bin/x86_64-w64-mingw32-windres")
set(CMAKE_FIND_ROOT_PATH "${_root}/x86_64-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
