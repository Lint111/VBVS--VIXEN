if(TARGET GTest::gtest_main)
    add_executable(test_text_glyph_metrics
        test_glyph_metrics.cpp
    )
    target_link_libraries(test_text_glyph_metrics PRIVATE
        GTest::gtest_main
        Text
    )
    target_compile_definitions(test_text_glyph_metrics PRIVATE
        TEXT_LATO_FONT_PATH="${CMAKE_CURRENT_LIST_DIR}/../../RenderGraph/assets/ui/LatoLatin-Regular.ttf"
    )
    set_target_properties(test_text_glyph_metrics PROPERTIES FOLDER "Tests/Text")
    vixen_gtest_discover_tests(Text test_text_glyph_metrics)
endif()
