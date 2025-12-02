# CLI tools
add_executable(tracesmith
    main.cpp
)

target_link_libraries(tracesmith PRIVATE
    tracesmith-common
    tracesmith-format
    tracesmith-capture
)

# Install CLI
install(TARGETS tracesmith
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
