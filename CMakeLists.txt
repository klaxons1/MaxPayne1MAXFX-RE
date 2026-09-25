cmake_minimum_required(VERSION 3.16)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
project(maxfx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MAXFX_BUILD_VIEWER "Build the SDL3 OpenGL level viewer" ON)
option(MAXFX_STATIC_CRT "Link the static MSVC runtime (drop-in .exe on Windows)" ON)

if(MSVC AND MAXFX_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(maxfx_ldb STATIC
    src/maxfx/core/Stream.cpp
    src/maxfx/core/Fs.cpp
    src/maxfx/ldb/LdbReader.cpp
    src/maxfx/image/Image.cpp
)
target_include_directories(maxfx_ldb PUBLIC src third_party)

add_executable(ldb-dump src/apps/ldb_dump.cpp)
target_link_libraries(ldb-dump PRIVATE maxfx_ldb)

if(MAXFX_BUILD_VIEWER)
    find_package(SDL3 CONFIG QUIET)
    if(NOT SDL3_FOUND)
        include(FetchContent)
        FetchContent_Declare(SDL3
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG release-3.2.20
        )
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(SDL3)
    endif()

    add_executable(ldb-viewer
        src/apps/ldb_viewer.cpp
        src/viewer/GL.cpp
        src/viewer/Renderer.cpp
        src/viewer/App.cpp
    )
    target_include_directories(ldb-viewer PRIVATE src)
    target_link_libraries(ldb-viewer PRIVATE maxfx_ldb SDL3::SDL3)
    if(WIN32)
        target_link_libraries(ldb-viewer PRIVATE opengl32)
    elseif(UNIX AND NOT APPLE)
        target_link_libraries(ldb-viewer PRIVATE ${CMAKE_DL_LIBS})
    endif()
endif()
