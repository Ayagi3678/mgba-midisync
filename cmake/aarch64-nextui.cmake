# NextUI (TrimUI tg5040) toolchain: gcc 8.3, glibc 2.28, from
# https://github.com/LoveRetro/gcc-arm-8.3-aarch64-tg5040
# Set NEXTUI_TOOLCHAIN to the unpacked x86_64-aarch64-nextui-linux-gnu folder.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
if(NOT DEFINED ENV{NEXTUI_TOOLCHAIN})
	message(FATAL_ERROR "NEXTUI_TOOLCHAIN is not set")
endif()
set(_tc "$ENV{NEXTUI_TOOLCHAIN}")
set(CMAKE_C_COMPILER "${_tc}/bin/aarch64-nextui-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "${_tc}/bin/aarch64-nextui-linux-gnu-g++")
set(CMAKE_SYSROOT "${_tc}/aarch64-nextui-linux-gnu/libc")
set(CMAKE_FIND_ROOT_PATH "${_tc}/aarch64-nextui-linux-gnu/libc")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
