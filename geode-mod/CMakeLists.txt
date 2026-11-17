cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
project(BismuthGeode VERSION 0.9.1 LANGUAGES C CXX)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK is not set")
endif()

# The Geode package is intentionally UI-only while native Cocos hook
# installation is being revalidated. Do not link Dobby or compile the
# experimental renderer adapter into this package.
file(GLOB_RECURSE GEODE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${GEODE_SOURCES})

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
setup_geode_mod(${PROJECT_NAME})
