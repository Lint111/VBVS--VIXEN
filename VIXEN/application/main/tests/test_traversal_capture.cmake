if(NOT BUILD_TESTS)
    return()
endif()

# R716 witness tool: default-scene offscreen capture with HitRecord readback. Driven by
# VIXEN_TRAVCAP_* (see the source header); not registered with CTest because it writes
# evidence to a caller-chosen path rather than asserting a fixed image.
if(TARGET VixenApp AND TARGET GTest::gtest_main)
    add_executable(test_traversal_capture
        ${CMAKE_CURRENT_LIST_DIR}/test_traversal_capture.cpp)
    vixen_whole_archive_link_vixen_app(test_traversal_capture PRIVATE)
    target_link_libraries(test_traversal_capture PRIVATE GTest::gtest_main)
    set_target_properties(test_traversal_capture PROPERTIES FOLDER "Tests/Application")
endif()
