# TraceSmith Tracy Integration
# Provides bidirectional integration between TraceSmith and Tracy Profiler

set(TRACY_SOURCES
    tracy_exporter.cpp
    tracy_importer.cpp
)

# Create Tracy integration library
add_library(tracesmith-tracy STATIC ${TRACY_SOURCES})

# Include directories
target_include_directories(tracesmith-tracy PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Link to common library
target_link_libraries(tracesmith-tracy PUBLIC
    tracesmith-common
)

# Tracy client library integration
if(TRACESMITH_ENABLE_TRACY)
    # Tracy configuration
    set(TRACY_ENABLE ON)
    set(TRACY_ON_DEMAND ON CACHE BOOL "Enable Tracy on-demand mode")
    set(TRACY_NO_EXIT OFF CACHE BOOL "Tracy no-exit mode")
    set(TRACY_NO_FRAME_IMAGE ON CACHE BOOL "Disable frame image capture")
    
    # Add Tracy client library
    set(TRACY_CLIENT_DIR ${CMAKE_SOURCE_DIR}/third_party/tracy/public)
    
    if(EXISTS ${TRACY_CLIENT_DIR}/TracyClient.cpp)
        # Build Tracy client as a static library
        add_library(tracy_client STATIC
            ${TRACY_CLIENT_DIR}/TracyClient.cpp
        )
        
        target_include_directories(tracy_client PUBLIC
            ${CMAKE_SOURCE_DIR}/third_party/tracy/public
        )
        
        # Tracy compile definitions
        target_compile_definitions(tracy_client PUBLIC
            TRACY_ENABLE
        )
        
        if(TRACY_ON_DEMAND)
            target_compile_definitions(tracy_client PUBLIC TRACY_ON_DEMAND)
        endif()
        
        if(TRACY_NO_FRAME_IMAGE)
            target_compile_definitions(tracy_client PUBLIC TRACY_NO_FRAME_IMAGE)
        endif()
        
        # Platform-specific settings
        if(WIN32)
            target_link_libraries(tracy_client PUBLIC ws2_32 dbghelp)
        elseif(UNIX AND NOT APPLE)
            find_package(Threads REQUIRED)
            target_link_libraries(tracy_client PUBLIC Threads::Threads dl)
        elseif(APPLE)
            find_package(Threads REQUIRED)
            target_link_libraries(tracy_client PUBLIC Threads::Threads)
        endif()
        
        # Disable warnings for third-party code
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
            target_compile_options(tracy_client PRIVATE -w)
        elseif(MSVC)
            target_compile_options(tracy_client PRIVATE /w)
        endif()
        
        # Link Tracy client to our integration library
        target_link_libraries(tracesmith-tracy PUBLIC tracy_client)
        target_compile_definitions(tracesmith-tracy PUBLIC TRACY_ENABLE)
        
        message(STATUS "Tracy client library enabled")
    else()
        message(WARNING "Tracy client source not found at ${TRACY_CLIENT_DIR}")
        message(WARNING "Tracy integration will have limited functionality")
    endif()
endif()

# CUDA support for Tracy GPU profiling
if(TRACESMITH_ENABLE_CUDA AND TRACESMITH_ENABLE_TRACY)
    if(CUPTI_FOUND)
        target_compile_definitions(tracesmith-tracy PUBLIC
            TRACESMITH_TRACY_CUDA
        )
        target_include_directories(tracesmith-tracy PUBLIC
            ${CUPTI_INCLUDE_DIRS}
        )
        target_link_libraries(tracesmith-tracy PUBLIC
            ${CUPTI_LIBRARIES}
        )
    endif()
endif()

# Installation
install(TARGETS tracesmith-tracy
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/tracesmith/tracy
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tracesmith
)
