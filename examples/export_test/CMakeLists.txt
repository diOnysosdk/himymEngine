add_executable(export_test
    main.cpp
)

target_link_libraries(export_test PRIVATE
    rev_platform
    rev_editor
    rev_curve
    rev_sequence
    imgui
)

# Copy to bin directory
set_target_properties(export_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
)
