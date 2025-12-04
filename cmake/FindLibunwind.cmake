# FindLibunwind.cmake
# Find the libunwind library
#
# This module defines:
#  LIBUNWIND_FOUND - system has libunwind
#  LIBUNWIND_INCLUDE_DIRS - the libunwind include directories
#  LIBUNWIND_LIBRARIES - link these to use libunwind
#  Libunwind::libunwind - imported target (INTERFACE library)

find_path(LIBUNWIND_INCLUDE_DIR
    NAMES libunwind.h
    PATHS
        /usr/include
        /usr/local/include
        /opt/homebrew/include
        /opt/local/include
    PATH_SUFFIXES libunwind
)

find_library(LIBUNWIND_LIBRARY
    NAMES unwind
    PATHS
        /usr/lib
        /usr/lib/x86_64-linux-gnu
        /usr/lib/aarch64-linux-gnu
        /usr/local/lib
        /opt/homebrew/lib
        /opt/local/lib
)

# Initialize the libraries list
set(LIBUNWIND_LIBRARIES "")

if(LIBUNWIND_LIBRARY)
    list(APPEND LIBUNWIND_LIBRARIES ${LIBUNWIND_LIBRARY})
endif()

# Handle platform-specific libunwind variants on Linux
if(CMAKE_SYSTEM_NAME MATCHES "Linux" AND LIBUNWIND_LIBRARY)
    find_library(LIBUNWIND_GENERIC_LIBRARY
        NAMES unwind-generic
        PATHS
            /usr/lib
            /usr/lib/x86_64-linux-gnu
            /usr/lib/aarch64-linux-gnu
            /usr/local/lib
    )
    if(LIBUNWIND_GENERIC_LIBRARY)
        list(APPEND LIBUNWIND_LIBRARIES ${LIBUNWIND_GENERIC_LIBRARY})
    endif()
    
    # Architecture-specific library
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
        find_library(LIBUNWIND_ARCH_LIBRARY
            NAMES unwind-x86_64
            PATHS
                /usr/lib
                /usr/lib/x86_64-linux-gnu
                /usr/local/lib
        )
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        find_library(LIBUNWIND_ARCH_LIBRARY
            NAMES unwind-aarch64
            PATHS
                /usr/lib
                /usr/lib/aarch64-linux-gnu
                /usr/local/lib
        )
    endif()
    
    if(LIBUNWIND_ARCH_LIBRARY)
        list(APPEND LIBUNWIND_LIBRARIES ${LIBUNWIND_ARCH_LIBRARY})
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Libunwind
    REQUIRED_VARS
        LIBUNWIND_LIBRARY
        LIBUNWIND_INCLUDE_DIR
)

if(LIBUNWIND_FOUND)
    set(LIBUNWIND_INCLUDE_DIRS ${LIBUNWIND_INCLUDE_DIR})
    
    # Create an INTERFACE imported target that links all the libraries
    if(NOT TARGET Libunwind::libunwind)
        add_library(Libunwind::libunwind INTERFACE IMPORTED)
        set_target_properties(Libunwind::libunwind PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBUNWIND_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "${LIBUNWIND_LIBRARIES}"
        )
    endif()
    
    mark_as_advanced(
        LIBUNWIND_INCLUDE_DIR
        LIBUNWIND_LIBRARY
        LIBUNWIND_GENERIC_LIBRARY
        LIBUNWIND_ARCH_LIBRARY
    )
endif()
