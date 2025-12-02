# FindCUPTI.cmake
# 
# Locate NVIDIA CUPTI (CUDA Profiling Tools Interface)
#
# This module defines:
#   CUPTI_FOUND        - True if CUPTI was found
#   CUPTI_INCLUDE_DIRS - CUPTI include directories
#   CUPTI_LIBRARIES    - CUPTI libraries
#   CUPTI_VERSION      - CUPTI version string
#
# Usage:
#   find_package(CUPTI)
#   if(CUPTI_FOUND)
#       include_directories(${CUPTI_INCLUDE_DIRS})
#       target_link_libraries(myapp ${CUPTI_LIBRARIES})
#   endif()

# Find CUDA first
find_package(CUDAToolkit QUIET)

if(CUDAToolkit_FOUND)
    # CUPTI is part of the CUDA Toolkit
    # Look for cupti.h header
    find_path(CUPTI_INCLUDE_DIR
        NAMES cupti.h
        HINTS
            ${CUDAToolkit_INCLUDE_DIRS}
            ${CUDA_TOOLKIT_ROOT_DIR}/extras/CUPTI/include
            ${CUDA_TOOLKIT_ROOT_DIR}/include
            /usr/local/cuda/extras/CUPTI/include
            /usr/local/cuda/include
            /opt/cuda/extras/CUPTI/include
        PATH_SUFFIXES
            CUPTI
            extras/CUPTI/include
    )
    
    # Look for libcupti library
    find_library(CUPTI_LIBRARY
        NAMES cupti
        HINTS
            ${CUDAToolkit_LIBRARY_DIR}
            ${CUDA_TOOLKIT_ROOT_DIR}/extras/CUPTI/lib64
            ${CUDA_TOOLKIT_ROOT_DIR}/lib64
            ${CUDA_TOOLKIT_ROOT_DIR}/lib
            /usr/local/cuda/extras/CUPTI/lib64
            /usr/local/cuda/lib64
            /opt/cuda/extras/CUPTI/lib64
        PATH_SUFFIXES
            CUPTI/lib64
            extras/CUPTI/lib64
    )
    
    # Get version from cupti_version.h if available
    if(CUPTI_INCLUDE_DIR)
        if(EXISTS "${CUPTI_INCLUDE_DIR}/cupti_version.h")
            file(READ "${CUPTI_INCLUDE_DIR}/cupti_version.h" CUPTI_VERSION_FILE)
            string(REGEX MATCH "CUPTI_API_VERSION[ \t]+([0-9]+)" _ ${CUPTI_VERSION_FILE})
            set(CUPTI_VERSION ${CMAKE_MATCH_1})
        endif()
    endif()
endif()

# Handle standard find_package arguments
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CUPTI
    REQUIRED_VARS
        CUPTI_LIBRARY
        CUPTI_INCLUDE_DIR
    VERSION_VAR
        CUPTI_VERSION
)

if(CUPTI_FOUND)
    set(CUPTI_INCLUDE_DIRS ${CUPTI_INCLUDE_DIR} ${CUDAToolkit_INCLUDE_DIRS})
    set(CUPTI_LIBRARIES ${CUPTI_LIBRARY} CUDA::cuda_driver)
    
    # Create imported target
    if(NOT TARGET CUPTI::cupti)
        add_library(CUPTI::cupti UNKNOWN IMPORTED)
        set_target_properties(CUPTI::cupti PROPERTIES
            IMPORTED_LOCATION "${CUPTI_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${CUPTI_INCLUDE_DIRS}"
        )
    endif()
    
    mark_as_advanced(CUPTI_INCLUDE_DIR CUPTI_LIBRARY)
endif()
