cmake_minimum_required(VERSION 3.15...4.2 FATAL_ERROR)

add_library(VoxelDocument STATIC
    include/EditorDocumentModel.h
    src/EditorDocumentModel.cpp
)

target_compile_features(VoxelDocument PUBLIC cxx_std_23)
target_include_directories(VoxelDocument
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(VoxelDocument PUBLIC SVO)
set_target_properties(VoxelDocument PROPERTIES FOLDER "Libraries")

if(MSVC)
    target_compile_options(VoxelDocument PRIVATE /W4 /WX)
else()
    target_compile_options(VoxelDocument PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
