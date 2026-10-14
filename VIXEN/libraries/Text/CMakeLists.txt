cmake_minimum_required(VERSION 3.20...4.2)
project(Text VERSION 1.0.0 LANGUAGES CXX)

set(TEXT_HEADERS
    include/Text/Abi.h
    include/Text/AtlasFormat.h
    include/Text/GlyphAtlas.h
    include/Text/RmlFontEngine.h
)

set(TEXT_SOURCES
    src/GlyphAtlas.cpp
    src/RmlFontEngine.cpp
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
)

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
