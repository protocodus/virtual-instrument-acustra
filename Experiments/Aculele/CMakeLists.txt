cmake_minimum_required(VERSION 3.22)

# Aculele: an experimental ukulele forked from Acustra's DSP. Prototype only:
# the JUCE-free engine and performer, the demo renderer and a probe tool. No
# plug-in, no editor. It is a separate CMake project that Acustra's own
# build does not include.
project(Aculele VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Threads REQUIRED)

add_library(AculeleDSP STATIC
    Source/DSP/AculeleEngine.cpp
    Source/DSP/AculeleEngine.h
    Source/DSP/AculelePerformer.cpp
    Source/DSP/AculelePerformer.h
    Source/DSP/AculelePresets.h)
target_include_directories(AculeleDSP PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_compile_features(AculeleDSP PUBLIC cxx_std_20)
if(NOT MSVC)
    target_compile_options(AculeleDSP PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The Rack toolchain is C++17 with -Werror; the fork keeps that constraint.
if(NOT MSVC)
    add_library(AculeleDSPCxx17 OBJECT
        Source/DSP/AculeleEngine.cpp
        Source/DSP/AculelePerformer.cpp)
    target_include_directories(AculeleDSPCxx17 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
    set_target_properties(AculeleDSPCxx17 PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_compile_options(AculeleDSPCxx17 PRIVATE -Wall -Wextra -Werror)
endif()

add_executable(RenderAculelePresetDemos Tools/RenderPresetDemos.cpp)
target_include_directories(RenderAculelePresetDemos PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Tools")
target_link_libraries(RenderAculelePresetDemos PRIVATE AculeleDSP Threads::Threads)

add_executable(AculeleProbe Tools/AculeleProbe.cpp)
target_link_libraries(AculeleProbe PRIVATE AculeleDSP)

add_executable(AculeleBodyModes Tools/AculeleBodyModes.cpp)
target_link_libraries(AculeleBodyModes PRIVATE AculeleDSP)
