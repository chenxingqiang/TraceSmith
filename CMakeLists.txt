cmake_minimum_required(VERSION 3.16)
project(TraceSmith 
    VERSION 0.8.2
    DESCRIPTION "Cross-platform GPU Profiling & Replay System"
    LANGUAGES CXX
)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows-specific settings
if(WIN32)
    # Prevent min/max macros from conflicting with std::min/std::max
    add_definitions(-DNOMINMAX)
    # Suppress some common MSVC warnings
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

# Build options
option(TRACESMITH_BUILD_TESTS "Build unit tests" ON)
option(TRACESMITH_BUILD_EXAMPLES "Build examples" ON)
option(TRACESMITH_BUILD_CLI "Build CLI tools" ON)
option(TRACESMITH_BUILD_PYTHON "Build Python bindings" OFF)
option(TRACESMITH_ENABLE_CUDA "Enable CUDA/CUPTI support" OFF)
option(TRACESMITH_ENABLE_ROCM "Enable ROCm support" OFF)
option(TRACESMITH_ENABLE_METAL "Enable Metal support" OFF)
option(TRACESMITH_ENABLE_MACA "Enable MetaX MACA/MCPTI support" OFF)
option(TRACESMITH_USE_LIBUNWIND "Use libunwind for call stack capture" ON)
option(TRACESMITH_USE_PERFETTO_SDK "Use Perfetto SDK for protobuf export" OFF)

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Third-party dependencies
add_subdirectory(third_party)

# GPU Platform Detection
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# CUDA/CUPTI Support
if(TRACESMITH_ENABLE_CUDA)
    find_package(CUPTI)
    if(CUPTI_FOUND)
        message(STATUS "CUPTI found: ${CUPTI_VERSION}")
        message(STATUS "  Include: ${CUPTI_INCLUDE_DIRS}")
        message(STATUS "  Library: ${CUPTI_LIBRARIES}")
        add_definitions(-DTRACESMITH_ENABLE_CUDA)
    else()
        message(WARNING "CUPTI not found, CUDA support disabled")
        set(TRACESMITH_ENABLE_CUDA OFF)
    endif()
endif()

# MetaX MACA/MCPTI Support
if(TRACESMITH_ENABLE_MACA)
    if(NOT MACA_ROOT)
        # Try to find MACA SDK in common locations
        foreach(MACA_PATH "/opt/maca-3.0.0" "/opt/maca" "/usr/local/maca")
            if(EXISTS "${MACA_PATH}/include/mcpti/mcpti.h")
                set(MACA_ROOT "${MACA_PATH}" CACHE PATH "Path to MACA SDK")
                break()
            endif()
        endforeach()
    endif()
    
    if(MACA_ROOT AND EXISTS "${MACA_ROOT}/include/mcpti/mcpti.h")
        message(STATUS "MACA SDK found at: ${MACA_ROOT}")
        add_definitions(-DTRACESMITH_ENABLE_MACA)
    else()
        message(WARNING "MACA SDK not found, MetaX support disabled. Set MACA_ROOT to the correct path.")
        set(TRACESMITH_ENABLE_MACA OFF)
    endif()
endif()

# libunwind Support (Cross-platform stack unwinding)
if(TRACESMITH_USE_LIBUNWIND)
    find_package(Libunwind)
    if(LIBUNWIND_FOUND)
        message(STATUS "libunwind found: ${LIBUNWIND_VERSION}")
        message(STATUS "  Include: ${LIBUNWIND_INCLUDE_DIRS}")
        message(STATUS "  Library: ${LIBUNWIND_LIBRARIES}")
        add_definitions(-DTRACESMITH_USE_LIBUNWIND)
    else()
        message(STATUS "libunwind not found, using platform-specific backtrace")
        set(TRACESMITH_USE_LIBUNWIND OFF)
    endif()
endif()

# Perfetto SDK Support (Native protobuf export)
if(TRACESMITH_USE_PERFETTO_SDK)
    set(PERFETTO_SDK_DIR ${CMAKE_SOURCE_DIR}/third_party/perfetto)
    if(EXISTS ${PERFETTO_SDK_DIR}/perfetto.h AND EXISTS ${PERFETTO_SDK_DIR}/perfetto.cc)
        message(STATUS "Perfetto SDK found")
        message(STATUS "  Location: ${PERFETTO_SDK_DIR}")
        add_definitions(-DTRACESMITH_PERFETTO_SDK_ENABLED)
        # Create perfetto library target
        add_library(perfetto_sdk STATIC ${PERFETTO_SDK_DIR}/perfetto.cc)
        target_include_directories(perfetto_sdk PUBLIC ${PERFETTO_SDK_DIR})
        # Perfetto SDK requires C++11 minimum, we use C++17
        target_compile_features(perfetto_sdk PUBLIC cxx_std_17)
        # Disable warnings for third-party code
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
            target_compile_options(perfetto_sdk PRIVATE -w)
        elseif(MSVC)
            target_compile_options(perfetto_sdk PRIVATE /w)
        endif()
    else()
        message(WARNING "Perfetto SDK files not found at ${PERFETTO_SDK_DIR}")
        message(WARNING "  Looking for: perfetto.h and perfetto.cc")
        message(WARNING "  Disabling Perfetto SDK support")
        set(TRACESMITH_USE_PERFETTO_SDK OFF)
    endif()
endif()

# Metal Support (macOS/iOS)
if(TRACESMITH_ENABLE_METAL)
    if(APPLE)
        find_library(METAL_LIBRARY Metal)
        find_library(FOUNDATION_LIBRARY Foundation)
        if(METAL_LIBRARY AND FOUNDATION_LIBRARY)
            message(STATUS "Metal framework found")
            message(STATUS "  Metal: ${METAL_LIBRARY}")
            message(STATUS "  Foundation: ${FOUNDATION_LIBRARY}")
            add_definitions(-DTRACESMITH_ENABLE_METAL)
        else()
            message(WARNING "Metal framework not found, Metal support disabled")
            set(TRACESMITH_ENABLE_METAL OFF)
        endif()
    else()
        message(WARNING "Metal is only available on Apple platforms")
        set(TRACESMITH_ENABLE_METAL OFF)
    endif()
endif()

# Core libraries
add_subdirectory(src/common)
add_subdirectory(src/format)
add_subdirectory(src/capture)
add_subdirectory(src/state)
add_subdirectory(src/replay)
add_subdirectory(src/cluster)

# CLI tools
if(TRACESMITH_BUILD_CLI)
    add_subdirectory(cli)
endif()

# Tests
if(TRACESMITH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
if(TRACESMITH_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Python bindings
if(TRACESMITH_BUILD_PYTHON)
    add_subdirectory(python)
endif()

# Installation
include(GNUInstallDirs)

install(DIRECTORY include/tracesmith
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Package configuration
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/TraceSmithConfig.cmake.in
    ${CMAKE_BINARY_DIR}/TraceSmithConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TraceSmith
)

write_basic_package_version_file(
    ${CMAKE_BINARY_DIR}/TraceSmithConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_BINARY_DIR}/TraceSmithConfig.cmake
    ${CMAKE_BINARY_DIR}/TraceSmithConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TraceSmith
)

# Summary
message(STATUS "")
message(STATUS "TraceSmith v${PROJECT_VERSION} Configuration:")
message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build tests:    ${TRACESMITH_BUILD_TESTS}")
message(STATUS "  Build examples: ${TRACESMITH_BUILD_EXAMPLES}")
message(STATUS "  Build CLI:      ${TRACESMITH_BUILD_CLI}")
message(STATUS "  Build Python:   ${TRACESMITH_BUILD_PYTHON}")
if(CUPTI_FOUND)
    message(STATUS "  CUDA support:   ${TRACESMITH_ENABLE_CUDA} (CUPTI ${CUPTI_VERSION})")
else()
    message(STATUS "  CUDA support:   ${TRACESMITH_ENABLE_CUDA}")
endif()
message(STATUS "  ROCm support:   ${TRACESMITH_ENABLE_ROCM}")
message(STATUS "  Metal support:  ${TRACESMITH_ENABLE_METAL}")
message(STATUS "  MACA support:   ${TRACESMITH_ENABLE_MACA}")
message(STATUS "  Perfetto SDK:   ${TRACESMITH_USE_PERFETTO_SDK}")
message(STATUS "  libunwind:      ${TRACESMITH_USE_LIBUNWIND}")
message(STATUS "")
