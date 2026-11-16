cmake_minimum_required(VERSION 3.20)
project(BismuthAndroid VERSION 0.1.0 LANGUAGES C CXX)
option(BISMUTH_BUILD_TESTS "Build host-side reference tests" ON)
option(BISMUTH_ENABLE_GLES31 "Build the Android OpenGL ES 3.1 backend" OFF)
option(BISMUTH_ENABLE_COCOS_HOOKS "Build the build-ID-gated passive Cocos2d hook adapter" ON)

# Generate embedded shaders from the authoritative shader files.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/classify.comp"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/group_offsets.comp"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scatter.comp"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/atlas_indices.comp"
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/atlas_candidates.comp")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/shaders/classify.comp" BISMUTH_CLASSIFY_SHADER)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/shaders/group_offsets.comp" BISMUTH_OFFSETS_SHADER)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scatter.comp" BISMUTH_SCATTER_SHADER)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/shaders/atlas_indices.comp" BISMUTH_ATLAS_INDICES_SHADER)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/shaders/atlas_candidates.comp" BISMUTH_ATLAS_CANDIDATES_SHADER)
set(BISMUTH_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${BISMUTH_GENERATED_INCLUDE_DIR}")
file(WRITE "${BISMUTH_GENERATED_INCLUDE_DIR}/embedded_shaders.hpp"
"#pragma once
namespace bismuth::shaders {
inline constexpr const char* classify = R\"BSHADER(${BISMUTH_CLASSIFY_SHADER})BSHADER\";
inline constexpr const char* offsets = R\"BSHADER(${BISMUTH_OFFSETS_SHADER})BSHADER\";
inline constexpr const char* scatter = R\"BSHADER(${BISMUTH_SCATTER_SHADER})BSHADER\";
inline constexpr const char* atlasIndices = R\"BSHADER(${BISMUTH_ATLAS_INDICES_SHADER})BSHADER\";
inline constexpr const char* atlasCandidates = R\"BSHADER(${BISMUTH_ATLAS_CANDIDATES_SHADER})BSHADER\";
}
")

add_library(bismuth_core STATIC src/culling_reference.cpp)
set_target_properties(bismuth_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(bismuth_core PUBLIC include)
target_compile_features(bismuth_core PUBLIC cxx_std_17)
target_compile_options(bismuth_core PRIVATE -Wall -Wextra -Wpedantic)

if(ANDROID AND BISMUTH_ENABLE_GLES31)
  add_library(bismuth_gles31 STATIC src/gles31_culler.cpp)
  set_target_properties(bismuth_gles31 PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_include_directories(bismuth_gles31 PUBLIC include PRIVATE "${BISMUTH_GENERATED_INCLUDE_DIR}")
  target_compile_features(bismuth_gles31 PUBLIC cxx_std_17)
  target_compile_definitions(bismuth_gles31 PRIVATE BISMUTH_ANDROID_GLES31=1)
  target_link_libraries(bismuth_gles31 PUBLIC bismuth_core GLESv3)

  # Build the native bootstrap; passive Cocos hooks install only when the exported API is called.
  if(BISMUTH_ENABLE_COCOS_HOOKS)
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
  endif()

  add_library(bismuth_mod SHARED src/android_entry.cpp)
  target_compile_features(bismuth_mod PRIVATE cxx_std_17)
  target_compile_definitions(bismuth_mod PRIVATE BISMUTH_ANDROID_GLES31=1)
  if(BISMUTH_ENABLE_COCOS_HOOKS)
    target_sources(bismuth_mod PRIVATE src/cocos_hooks.cpp)
    target_compile_definitions(bismuth_mod PRIVATE BISMUTH_ENABLE_COCOS_HOOKS=1)
    target_link_libraries(bismuth_mod PRIVATE dobby dl)
  endif()
  target_link_libraries(bismuth_mod PRIVATE bismuth_gles31 bismuth_core log)
  set_target_properties(bismuth_mod PROPERTIES OUTPUT_NAME "bismuth")
endif()

if(BISMUTH_BUILD_TESTS)
  enable_testing()
  add_executable(bismuth_reference_tests tests/culling_reference_tests.cpp)
  target_link_libraries(bismuth_reference_tests PRIVATE bismuth_core)
  add_test(NAME culling_reference_tests COMMAND bismuth_reference_tests)
  add_executable(bismuth_android_api_c_test tests/android_api_header_c_test.c)
  target_include_directories(bismuth_android_api_c_test PRIVATE include)
  add_test(NAME android_api_header_c_test COMMAND bismuth_android_api_c_test)
endif()
