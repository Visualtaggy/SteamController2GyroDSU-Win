cmake_minimum_required(VERSION 3.16)
project(sc2gyrodsu CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

find_library(HIDAPI_LIB hidapi-hidraw REQUIRED)
find_path(HIDAPI_INC hidapi/hidapi.h REQUIRED)

# ── Daemon: sc2gyrodsu ────────────────────────────────────────────────────────

add_executable(sc2gyrodsu
    src/main.cpp
    src/hiddev.cpp
    src/triton.cpp
    src/dsu.cpp
    src/config.cpp
)

target_include_directories(sc2gyrodsu PRIVATE inc ${HIDAPI_INC})
target_link_libraries(sc2gyrodsu PRIVATE
    Threads::Threads
    ${HIDAPI_LIB}
)
target_compile_options(sc2gyrodsu PRIVATE -O3 -Wall -Wextra)

# ── GUI configuration tool: sc2gyrodsu-config ─────────────────────────────────

find_package(Qt6 COMPONENTS Core Widgets QUIET)

if(Qt6_FOUND)
    message(STATUS "Qt6 found — building sc2gyrodsu-config")

    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)

    add_executable(sc2gyrodsu-config
        src/gui/configapp.cpp
        src/gui/mainwindow.cpp
        src/gui/calibworker.cpp
        src/config.cpp
        src/triton.cpp
    )

    target_include_directories(sc2gyrodsu-config PRIVATE inc src ${HIDAPI_INC})
    target_link_libraries(sc2gyrodsu-config PRIVATE
        Qt6::Core
        Qt6::Widgets
        ${HIDAPI_LIB}
        Threads::Threads
    )
    target_compile_options(sc2gyrodsu-config PRIVATE -O2 -Wall -Wextra)

    install(TARGETS sc2gyrodsu-config DESTINATION bin)
else()
    message(STATUS "Qt6 not found — skipping sc2gyrodsu-config (install qt6-base-dev to enable)")
endif()

install(TARGETS sc2gyrodsu DESTINATION bin)
