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

    add_executable(test_text_shaping
        test_text_shaping.cpp
    )
    target_link_libraries(test_text_shaping PRIVATE
        GTest::gtest_main
        Text
    )
    target_compile_definitions(test_text_shaping PRIVATE
        TEXT_LATO_FONT_PATH="${CMAKE_CURRENT_LIST_DIR}/../../RenderGraph/assets/ui/LatoLatin-Regular.ttf"
        TEXT_NOTO_ARABIC_FONT_PATH="${CMAKE_CURRENT_LIST_DIR}/data/NotoSansArabic-VF.ttf"
    )
    if(MSVC)
        target_compile_options(test_text_shaping PRIVATE /W4 /WX)
    else()
        target_compile_options(test_text_shaping PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    set_target_properties(test_text_shaping PROPERTIES FOLDER "Tests/Text")
    vixen_gtest_discover_tests(Text test_text_shaping)
endif()
vixen_schedule_runtime_dlls_for_current_directory()
