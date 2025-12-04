# Python bindings using pybind11
cmake_minimum_required(VERSION 3.16)

# Find Python
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

# Find or fetch pybind11
include(FetchContent)
FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG v2.11.1
)
FetchContent_MakeAvailable(pybind11)

# Create Python module
pybind11_add_module(_tracesmith
    src/bindings.cpp
)

target_link_libraries(_tracesmith PRIVATE
    tracesmith-common
    tracesmith-format
    tracesmith-capture
    tracesmith-state
    tracesmith-replay
    tracesmith-cluster
)

target_include_directories(_tracesmith PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Set output directory - use TRACESMITH_PYTHON_OUTPUT_DIR if set (from setup.py for pip install),
# otherwise use source directory for local development
if(TRACESMITH_PYTHON_OUTPUT_DIR)
    set_target_properties(_tracesmith PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${TRACESMITH_PYTHON_OUTPUT_DIR}
    )
    message(STATUS "Python module output: ${TRACESMITH_PYTHON_OUTPUT_DIR}")
else()
    set_target_properties(_tracesmith PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tracesmith
    )
    message(STATUS "Python module output: ${CMAKE_CURRENT_SOURCE_DIR}/tracesmith")
endif()

# Copy Python files after build
add_custom_command(TARGET _tracesmith POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/tracesmith/__init__.py
        $<TARGET_FILE_DIR:_tracesmith>/__init__.py
    COMMENT "Copying Python package files"
)
