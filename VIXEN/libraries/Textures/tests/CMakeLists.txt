cmake_minimum_required(VERSION 3.15)
project(TexturesTests)

include(GoogleTest)

add_executable(test_textures
    test_texture_module.cpp
)
target_compile_features(test_textures PRIVATE cxx_std_23)
target_compile_definitions(test_textures PRIVATE
    TEXTURES_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(test_textures PRIVATE GTest::gtest_main Textures)
set_target_properties(test_textures PROPERTIES FOLDER "Tests/Textures Tests")
vixen_gtest_discover_tests(Textures test_textures)
