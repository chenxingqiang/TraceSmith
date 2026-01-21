# Third-party dependencies

# GoogleTest (for testing)
if(TRACESMITH_BUILD_TESTS)
    include(FetchContent)
    
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    
    FetchContent_MakeAvailable(googletest)
endif()

# Tracy Profiler
# Tracy is included as a git submodule at third_party/tracy
# The actual Tracy client library is built in src/tracy/CMakeLists.txt
if(TRACESMITH_ENABLE_TRACY)
    set(TRACY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tracy)
    if(EXISTS ${TRACY_DIR}/public/TracyClient.cpp)
        message(STATUS "Tracy Profiler found at: ${TRACY_DIR}")
        set(TRACY_AVAILABLE ON CACHE BOOL "Tracy profiler is available" FORCE)
    else()
        message(WARNING "Tracy Profiler not found. Run: git submodule update --init")
        set(TRACY_AVAILABLE OFF CACHE BOOL "Tracy profiler is available" FORCE)
        set(TRACESMITH_ENABLE_TRACY OFF CACHE BOOL "Enable Tracy" FORCE)
    endif()
endif()
