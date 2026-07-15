cmake_minimum_required(VERSION 3.16)

# Statically link the MSVC runtime (/MT instead of /MD) so a Visual Studio
# build produces a standalone .exe with no dependency on the end user having
# the Visual C++ Redistributable installed. Must be set before project().
# No-op on non-MSVC toolchains; MinGW is statically linked separately via
# CMAKE_EXE_LINKER_FLAGS_INIT in mingw-toolchain.cmake.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(sc2gyrodsu CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# hidapi: fetched and built automatically (Windows backend on Windows,
# hidraw backend on Linux). No manual dependency install needed.
include(FetchContent)
set(HIDAPI_BUILD_HIDTEST OFF CACHE BOOL "" FORCE)
set(HIDAPI_WITH_LIBUSB OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    hidapi
    GIT_REPOSITORY https://github.com/libusb/hidapi.git
    GIT_TAG        hidapi-0.14.0
)
FetchContent_MakeAvailable(hidapi)

add_executable(sc2gyrodsu
    src/main.cpp
    src/hiddev.cpp
    src/triton.cpp
    src/dsu.cpp
)

target_include_directories(sc2gyrodsu PRIVATE inc ${hidapi_SOURCE_DIR})

if(WIN32)
    target_link_libraries(sc2gyrodsu PRIVATE Threads::Threads hidapi::winapi ws2_32)
else()
    target_link_libraries(sc2gyrodsu PRIVATE Threads::Threads hidapi::hidraw)
endif()

if(MSVC)
    target_compile_options(sc2gyrodsu PRIVATE /O2 /W3)
else()
    target_compile_options(sc2gyrodsu PRIVATE -O3 -Wall)
endif()
