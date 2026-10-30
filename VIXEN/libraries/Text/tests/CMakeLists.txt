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

    add_executable(test_text_glyph_cache
        test_glyph_cache.cpp
    )
    target_link_libraries(test_text_glyph_cache PRIVATE
        GTest::gtest_main
        Text
    )
    target_compile_definitions(test_text_glyph_cache PRIVATE
        TEXT_LATO_FONT_PATH="${CMAKE_CURRENT_LIST_DIR}/../../RenderGraph/assets/ui/LatoLatin-Regular.ttf"
    )
    if(MSVC)
        target_compile_options(test_text_glyph_cache PRIVATE /W4 /WX)
    else()
        target_compile_options(test_text_glyph_cache PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    set_target_properties(test_text_glyph_cache PROPERTIES FOLDER "Tests/Text")
    vixen_gtest_discover_tests(Text test_text_glyph_cache)

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

    add_executable(test_text_shader_layout
        test_text_shader_layout.cpp
    )
    target_link_libraries(test_text_shader_layout PRIVATE
        GTest::gtest_main
        Text
    )
    target_compile_definitions(test_text_shader_layout PRIVATE
        TEXT_SHADER_GLSL_PATH="${TEXT_SHADER_GLSL_INTERFACE}"
    )
    if(MSVC)
        target_compile_options(test_text_shader_layout PRIVATE /W4 /WX)
    else()
        target_compile_options(test_text_shader_layout PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    set_target_properties(test_text_shader_layout PROPERTIES FOLDER "Tests/Text")
    vixen_gtest_discover_tests(Text test_text_shader_layout)

    add_executable(test_rml_text_element
        test_rml_text_element.cpp
    )
    target_link_libraries(test_rml_text_element PRIVATE
        GTest::gtest_main
        Text
    )
    target_compile_definitions(test_rml_text_element PRIVATE
        TEXT_LATO_FONT_PATH="${CMAKE_CURRENT_LIST_DIR}/../../RenderGraph/assets/ui/LatoLatin-Regular.ttf"
    )
    if(MSVC)
        target_compile_options(test_rml_text_element PRIVATE /W4 /WX)
    else()
        target_compile_options(test_rml_text_element PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    set_target_properties(test_rml_text_element PROPERTIES FOLDER "Tests/Text")
    vixen_gtest_discover_tests(Text test_rml_text_element)
endif()
vixen_schedule_runtime_dlls_for_current_directory()
