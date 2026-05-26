cmake_minimum_required(VERSION 3.16)
project(silent_woods CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(raylib QUIET)

if (NOT raylib_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG        5.5
    )
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(raylib)
endif()

add_executable(silent_woods silent_woods.cpp)
target_include_directories(silent_woods PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
target_link_libraries(silent_woods PRIVATE raylib)

if (APPLE)
    target_link_libraries(silent_woods PRIVATE
        "-framework IOKit"
        "-framework Cocoa"
        "-framework OpenGL"
        "-framework CoreAudio"
        "-framework AudioToolbox"
    )
elseif (WIN32)
    target_link_libraries(silent_woods PRIVATE winmm ole32)
elseif (UNIX)
    target_link_libraries(silent_woods PRIVATE pthread dl m)
endif()

if (MSVC)
    target_compile_options(silent_woods PRIVATE /W3)
else()
    target_compile_options(silent_woods PRIVATE -Wno-unused-parameter -Wno-unused-variable)
endif()
