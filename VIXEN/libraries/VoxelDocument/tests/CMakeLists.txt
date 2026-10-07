add_executable(test_editor_document_model_preview
    test_editor_document_model_preview.cpp
)
target_compile_features(test_editor_document_model_preview PRIVATE cxx_std_23)
target_link_libraries(test_editor_document_model_preview PRIVATE VoxelDocument GTest::gtest_main)
target_compile_definitions(test_editor_document_model_preview PRIVATE
    VXD_GOLDEN_PATH="${VIXEN_ROOT}/BuiltAssets/documents/sample_tri_layer.vxd"
)
set_target_properties(test_editor_document_model_preview PROPERTIES FOLDER "Tests/VoxelDocument Tests")

# This CPU-only module test deliberately uses ordinary GoogleTest discovery so CTest does not
# attach the RenderGraph suite's Vulkan loader environment.
gtest_discover_tests(test_editor_document_model_preview)
