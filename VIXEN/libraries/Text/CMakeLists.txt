cmake_minimum_required(VERSION 3.20...4.2)
project(Text VERSION 1.0.0 LANGUAGES CXX)

set(TEXT_HEADERS
    include/Text/Abi.h
    include/Text/Text.g.h
    include/Text/TextShaderSpirv.g.h
    include/Text/AtlasFormat.h
    include/Text/GlyphAtlas.h
    include/Text/Bidi.h
    include/Text/LineBreaker.h
    include/Text/RmlFontEngine.h
    include/Text/Shaper.h
    src/shaders/text.g.glsl
    src/shaders/text.vert
    src/shaders/text.frag
)

set(TEXT_SOURCES
    src/Bidi.cpp
    src/GlyphAtlas.cpp
    src/LineBreaker.cpp
    src/RmlFontEngine.cpp
    src/Shaper.cpp
)

add_library(Text STATIC ${TEXT_HEADERS} ${TEXT_SOURCES})
target_include_directories(Text PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(Text PUBLIC cxx_std_23)
target_compile_definitions(Text PUBLIC RMLUI_STATIC_LIB)
set_target_properties(Text PROPERTIES FOLDER "Libraries")
target_link_libraries(Text
    PUBLIC
        EventBus
        Logger
        VulkanResources
        rmlui_core
        freetype
    PRIVATE
        msdfgen-core
        harfbuzz
        SheenBidi::SheenBidi
        VixenLibunibreak
)
if(MSVC)
    target_compile_options(Text PRIVATE /W4 /WX)
else()
    target_compile_options(Text PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
