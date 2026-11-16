cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
project(BismuthGeode VERSION 0.9.0 LANGUAGES C CXX)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK is not set")
endif()

set(BISMUTH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(BISMUTH_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${BISMUTH_GENERATED_INCLUDE_DIR}")
foreach(shader classify group_offsets scatter atlas_indices)
    file(READ "${BISMUTH_ROOT}/shaders/${shader}.comp" BISMUTH_${shader}_SOURCE)
endforeach()
file(WRITE "${BISMUTH_GENERATED_INCLUDE_DIR}/embedded_shaders.hpp"
"#pragma once
namespace bismuth::shaders {
inline constexpr const char* classify = R\"BSHADER(${BISMUTH_classify_SOURCE})BSHADER\";
inline constexpr const char* offsets = R\"BSHADER(${BISMUTH_group_offsets_SOURCE})BSHADER\";
inline constexpr const char* scatter = R\"BSHADER(${BISMUTH_scatter_SOURCE})BSHADER\";
inline constexpr const char* atlasIndices = R\"BSHADER(${BISMUTH_atlas_indices_SOURCE})BSHADER\";
}
")

include(FetchContent)
set(DOBBY_DEBUG OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(Plugin.SymbolResolver OFF CACHE BOOL "" FORCE)
FetchContent_Declare(dobby
    GIT_REPOSITORY https://github.com/Linifadomra/Dobby.git
    GIT_TAG a418c6a7c493e6c599713a097ff823c84a40ba96
    GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(dobby)

file(GLOB_RECURSE GEODE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED
    ${GEODE_SOURCES}
    ${BISMUTH_ROOT}/src/culling_reference.cpp
    ${BISMUTH_ROOT}/src/gles31_culler.cpp
    ${BISMUTH_ROOT}/src/gpu_timer_query.cpp
    ${BISMUTH_ROOT}/src/cocos_hooks.cpp
)

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
setup_geode_mod(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${BISMUTH_ROOT}/include
    ${BISMUTH_GENERATED_INCLUDE_DIR}
)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    BISMUTH_ANDROID_GLES31=1
    BISMUTH_ENABLE_COCOS_HOOKS=1
)
target_link_libraries(${PROJECT_NAME} PRIVATE dobby GLESv3 EGL dl log)
