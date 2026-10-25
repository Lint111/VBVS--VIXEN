add_executable(test_editor_document_model_preview
    test_editor_document_model_preview.cpp
)
target_compile_features(test_editor_document_model_preview PRIVATE cxx_std_23)
target_link_libraries(test_editor_document_model_preview PRIVATE VoxelDocument AppFlow GTest::gtest_main)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/VoxelDocument-tests")
target_compile_definitions(test_editor_document_model_preview PRIVATE
    VXD_GOLDEN_PATH="${VIXEN_ROOT}/BuiltAssets/documents/sample_tri_layer.vxd"
    VXD_TEST_TEMP_DIR="${CMAKE_BINARY_DIR}/VoxelDocument-tests"
)
set_target_properties(test_editor_document_model_preview PROPERTIES FOLDER "Tests/VoxelDocument Tests")

# This CPU-only module test deliberately uses ordinary GoogleTest discovery so CTest does not
# attach the RenderGraph suite's Vulkan loader environment.
vixen_gtest_discover_tests(VoxelDocument test_editor_document_model_preview)
add_executable(test_pack_recipe_source
    test_pack_recipe_source.cpp
)
target_compile_features(test_pack_recipe_source PRIVATE cxx_std_23)
target_link_libraries(test_pack_recipe_source PRIVATE VoxelDocument GTest::gtest_main)
target_compile_definitions(test_pack_recipe_source PRIVATE
    VXD_GOLDEN_PATH="${VIXEN_ROOT}/BuiltAssets/documents/sample_tri_layer.vxd"
    VXD_TEST_TEMP_DIR="${CMAKE_BINARY_DIR}/VoxelDocument-tests"
)
set_target_properties(test_pack_recipe_source PROPERTIES FOLDER "Tests/VoxelDocument Tests")
vixen_gtest_discover_tests(VoxelDocument test_pack_recipe_source)
vixen_schedule_runtime_dlls_for_current_directory()
