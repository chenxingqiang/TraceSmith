# FindLibunwind.cmake
# Find the libunwind library
#
# This module defines:
#  LIBUNWIND_FOUND - system has libunwind
#  LIBUNWIND_INCLUDE_DIRS - the libunwind include directories
#  LIBUNWIND_LIBRARIES - link these to use libunwind
#  Libunwind::libunwind - imported target

find_path(LIBUNWIND_INCLUDE_DIR
    NAMES libunwind.h
    PATHS
        /usr/include
        /usr/local/include
        /opt/homebrew/include
        /opt/local/include
)

find_library(LIBUNWIND_LIBRARY
    NAMES unwind
    PATHS
        /usr/lib
        /usr/local/lib
        /opt/homebrew/lib
        /opt/local/lib
)

# Handle platform-specific libunwind variants
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_library(LIBUNWIND_GENERIC_LIBRARY
        NAMES unwind-generic
        PATHS
            /usr/lib
            /usr/local/lib
    )
    if(LIBUNWIND_GENERIC_LIBRARY)
        list(APPEND LIBUNWIND_LIBRARY ${LIBUNWIND_GENERIC_LIBRARY})
    endif()
    
    # Architecture-specific library
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
        find_library(LIBUNWIND_ARCH_LIBRARY
            NAMES unwind-x86_64
            PATHS /usr/lib /usr/local/lib
        )
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        find_library(LIBUNWIND_ARCH_LIBRARY
            NAMES unwind-aarch64
            PATHS /usr/lib /usr/local/lib
        )
    endif()
    
    if(LIBUNWIND_ARCH_LIBRARY)
        list(APPEND LIBUNWIND_LIBRARY ${LIBUNWIND_ARCH_LIBRARY})
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Libunwind
    REQUIRED_VARS
        LIBUNWIND_LIBRARY
        LIBUNWIND_INCLUDE_DIR
)

if(LIBUNWIND_FOUND)
    set(LIBUNWIND_LIBRARIES ${LIBUNWIND_LIBRARY})
    set(LIBUNWIND_INCLUDE_DIRS ${LIBUNWIND_INCLUDE_DIR})
    
    # Create imported target
    if(NOT TARGET Libunwind::libunwind)
        add_library(Libunwind::libunwind UNKNOWN IMPORTED)
        set_target_properties(Libunwind::libunwind PROPERTIES
            IMPORTED_LOCATION "${LIBUNWIND_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LIBUNWIND_INCLUDE_DIR}"
        )
    endif()
    
    mark_as_advanced(
        LIBUNWIND_INCLUDE_DIR
        LIBUNWIND_LIBRARY
        LIBUNWIND_GENERIC_LIBRARY
        LIBUNWIND_ARCH_LIBRARY
    )
endif()
