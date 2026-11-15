cmake_minimum_required(VERSION 3.20)
project(BismuthAndroid VERSION 0.1.0 LANGUAGES CXX)
option(BISMUTH_BUILD_TESTS "Build host-side reference tests" ON)
option(BISMUTH_ENABLE_GLES31 "Build the Android OpenGL ES 3.1 backend" OFF)

add_library(bismuth_core STATIC src/culling_reference.cpp)
set_target_properties(bismuth_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(bismuth_core PUBLIC include)
target_compile_features(bismuth_core PUBLIC cxx_std_17)
target_compile_options(bismuth_core PRIVATE -Wall -Wextra -Wpedantic)

if(ANDROID AND BISMUTH_ENABLE_GLES31)
  add_library(bismuth_gles31 STATIC src/gles31_culler.cpp)
  set_target_properties(bismuth_gles31 PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_include_directories(bismuth_gles31 PUBLIC include)
  target_compile_features(bismuth_gles31 PUBLIC cxx_std_17)
  target_compile_definitions(bismuth_gles31 PRIVATE BISMUTH_ANDROID_GLES31=1)
  target_link_libraries(bismuth_gles31 PUBLIC bismuth_core GLESv3)

  # Produces a loadable ARM64 shared object. This is a bootstrap library only;
  # no game renderer hook is installed until verified 2.2081 call sites exist.
  add_library(bismuth_mod SHARED src/android_entry.cpp)
  target_compile_features(bismuth_mod PRIVATE cxx_std_17)
  target_link_libraries(bismuth_mod PRIVATE bismuth_gles31 bismuth_core log)
  set_target_properties(bismuth_mod PROPERTIES OUTPUT_NAME "bismuth")
endif()

if(BISMUTH_BUILD_TESTS)
  enable_testing()
  add_executable(bismuth_reference_tests tests/culling_reference_tests.cpp)
  target_link_libraries(bismuth_reference_tests PRIVATE bismuth_core)
  add_test(NAME culling_reference_tests COMMAND bismuth_reference_tests)
endif()
