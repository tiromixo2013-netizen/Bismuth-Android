cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
project(BismuthGeode VERSION 0.9.6 LANGUAGES C CXX)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK is not set")
endif()

# This package contains the gameplay HUD, scene-owned update controller, and the
# passive SpriteBatch draw observer. The experimental native GLES renderer adapter
# remains a separate library and is intentionally not linked into the Geode mod.
file(GLOB_RECURSE GEODE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${GEODE_SOURCES})

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
setup_geode_mod(${PROJECT_NAME})
