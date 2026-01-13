cmake_minimum_required(VERSION 3.15)

# Enable vcpkg integration
set(VCPKG_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg")
if(NOT EXISTS "${VCPKG_ROOT_DIR}")
    message(STATUS "vcpkg not found. Cloning vcpkg...")
    execute_process(
            COMMAND git clone https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT_DIR}"
            RESULT_VARIABLE GIT_CLONE_RESULT
    )

    if(NOT GIT_CLONE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to clone vcpkg repository.")
    endif()

    message(STATUS "Bootstrapping vcpkg...")
    if(WIN32)
        execute_process(COMMAND "${VCPKG_ROOT_DIR}/bootstrap-vcpkg.bat")
    else()
        execute_process(COMMAND "${VCPKG_ROOT_DIR}/bootstrap-vcpkg.sh")
    endif()
endif()

# Set the toolchain file so CMake uses vcpkg for find_package()
set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT_DIR}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")

# Determine the correct static triplet based on the OS
if(WIN32)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
elseif(APPLE)
    set(VCPKG_TARGET_TRIPLET "x64-osx-static" CACHE STRING "") # Or arm64-osx-static
else()
    set(VCPKG_TARGET_TRIPLET "x64-linux" CACHE STRING "")
endif()

# Force vcpkg to use static linkage for the CRT (Required for Windows Static)
if(MSVC)
    set(VCPKG_CRT_LINKAGE "static" CACHE STRING "")
endif()

# Inform CMake to prefer static libraries globally
set(VCPKG_LIBRARY_LINKAGE "static" CACHE STRING "")

# Project definition
project(qubic CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include the centralized compiler detection module
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerSetup)

# Build options
option(BUILD_TESTS "Build the test suite" ON)
option(BUILD_BENCHMARK "Build the EFI benchmark application" OFF)
option(BUILD_BINARY "Build the EFI application" ON)
option(USE_SANITIZER "Build test with sanitizer support (clang only)" ON)

if(IS_WINDOWS)
    message(WARNING "Building on Windows using Visual Studio and CMake has undergone limited testing and is not officially supported at this time.")
    if(USE_SANITIZER)
        message(WARNING "Building tests with sanitizer support is not supported on Windows.")
    endif()
endif()

# Always include platform_common as it's needed for both application and tests
add_subdirectory(lib/platform_common)

if(BUILD_BINARY OR BUILD_BENCHMARK)
    add_subdirectory(lib/platform_efi)
endif()

# Build the tests first. On fail, the build will fail completely
if(BUILD_TESTS)
    message(STATUS "--- Test suite ---")
    enable_testing()
    add_subdirectory(lib/platform_os)

    # If we're not building the application, we still need src for tests
    # but don't make it a default target
    if(NOT BUILD_BINARY)
        add_subdirectory(src EXCLUDE_FROM_ALL)
    endif()

    add_subdirectory(test)
endif()

# Build the application if requested
if(BUILD_BINARY)
    message(STATUS "--- EFI Core application ---")
    add_subdirectory(src)
endif()

# Add the UEFI m256i benchmark
if(BUILD_BENCHMARK)
    message(STATUS "-- EFI Benchmark ---")
    add_subdirectory(benchmark_uefi)
endif()
