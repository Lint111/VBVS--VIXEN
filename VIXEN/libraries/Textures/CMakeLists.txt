cmake_minimum_required(VERSION 3.20)
project(Textures VERSION 1.0.0 LANGUAGES CXX)

add_library(Textures STATIC
    include/Textures/Residency.h
    include/Textures/STBTextureLoader.h
    include/Textures/TextureCacher.h
    include/Textures/TextureLoader.h
    src/Residency.cpp
    src/STBTextureLoader.cpp
    src/TextureCacher.cpp
    src/TextureLoader.cpp
)

target_compile_features(Textures PUBLIC cxx_std_23)

target_include_directories(Textures
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${VIXEN_ROOT}/generated/sdi>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/application/main/generated/sdi>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(Textures
    PUBLIC
        Core::Core
        Logger
        VulkanResources
        CashSystem
    PRIVATE
        ResourceManagement
        stb
)

set_target_properties(Textures PROPERTIES FOLDER "Libraries")

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
