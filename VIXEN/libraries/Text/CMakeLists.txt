cmake_minimum_required(VERSION 3.20...4.2)
project(Text VERSION 1.0.0 LANGUAGES CXX)

if(NOT DEFINED ENV{UNDERTOW_ROOT} OR "$ENV{UNDERTOW_ROOT}" STREQUAL "")
    message(FATAL_ERROR "Text requires UNDERTOW_ROOT so it can consume the generated text shader interface")
endif()
file(TO_CMAKE_PATH "$ENV{UNDERTOW_ROOT}" _undertow_root_input)
get_filename_component(_undertow_root "${_undertow_root_input}" REALPATH)
set(TEXT_SHADER_INTERFACE_ROOT "${_undertow_root}/transpiled/textshader")
set(TEXT_SHADER_INTERFACE_DIR "${TEXT_SHADER_INTERFACE_ROOT}/Text")
set(TEXT_SHADER_CPP_INTERFACE "${TEXT_SHADER_INTERFACE_DIR}/Text.g.h")
set(TEXT_SHADER_GLSL_INTERFACE "${TEXT_SHADER_INTERFACE_DIR}/text.g.glsl")
if(NOT EXISTS "${TEXT_SHADER_CPP_INTERFACE}" OR NOT EXISTS "${TEXT_SHADER_GLSL_INTERFACE}")
    message(FATAL_ERROR
        "Generated text shader interface is missing under ${TEXT_SHADER_INTERFACE_ROOT}; "
        "run the Undertow text shader interface generator before configuring VIXEN")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(_text_shader_regenerator
    "${VIXEN_ROOT}/libraries/RenderGraph/src/Ui/shaders/regenerate-ui-shader-header.py")
set(_text_shader_header "${CMAKE_CURRENT_SOURCE_DIR}/include/Text/TextShaderSpirv.g.h")
add_custom_command(
    OUTPUT "${_text_shader_header}"
    COMMAND "${Python3_EXECUTABLE}" "${_text_shader_regenerator}"
        --text-only --text-interface-dir "${TEXT_SHADER_INTERFACE_DIR}"
    DEPENDS
        "${_text_shader_regenerator}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/text.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/text.frag"
        "${TEXT_SHADER_CPP_INTERFACE}"
        "${TEXT_SHADER_GLSL_INTERFACE}"
    COMMENT "Compile VIXEN UI and text GLSL to checked-in SPIR-V headers"
    VERBATIM
)
add_custom_target(vixen_text_shader_headers
    DEPENDS "${_text_shader_header}")

set(TEXT_HEADERS
    include/Text/Abi.h
    "${TEXT_SHADER_CPP_INTERFACE}"
    include/Text/TextShaderSpirv.g.h
    include/Text/AtlasFormat.h
    include/Text/GlyphAtlas.h
    include/Text/GlyphCellStore.h
    include/Text/GlyphOutline.h
    include/Text/Bidi.h
    include/Text/LineBreaker.h
    include/Text/RmlFontEngine.h
    include/Text/RmlTextElement.h
    include/Text/Shaper.h
    "${TEXT_SHADER_GLSL_INTERFACE}"
    src/shaders/text.vert
    src/shaders/text.frag
)

set(TEXT_SOURCES
    src/Bidi.cpp
    src/GlyphAtlas.cpp
    src/GlyphCellStore.cpp
    src/GlyphOutline.cpp
    src/LineBreaker.cpp
    src/RmlFontEngine.cpp
    src/RmlTextElement.cpp
    src/Shaper.cpp
)

add_library(Text STATIC ${TEXT_HEADERS} ${TEXT_SOURCES})
add_dependencies(Text vixen_text_shader_headers)
target_include_directories(Text PUBLIC
    $<BUILD_INTERFACE:${TEXT_SHADER_INTERFACE_ROOT}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../SVO/include>
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
if(TARGET sdf_core_kernels_check)
    add_dependencies(Text sdf_core_kernels_check)
endif()
if(TARGET glyph_outline_instance_check)
    add_dependencies(Text glyph_outline_instance_check)
endif()
if(MSVC)
    target_compile_options(Text PRIVATE /W4 /WX)
else()
    target_compile_options(Text PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
