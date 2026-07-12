# View Contract Inc-2b Task 9 -- the proof gate. Links VixenApp (mirrors test_hud_view.cmake) which
# only exists after application/main/CMakeLists.txt has defined it; VixenApp already links
# RenderGraph PUBLIC so BlobView/ViewStore/ViewBlobFile resolve transitively alongside HudView.
if(NOT BUILD_TESTS)
    return()
endif()

if(TARGET VixenApp AND TARGET GTest::gtest_main)
    add_executable(test_view_blob_equiv ${CMAKE_CURRENT_LIST_DIR}/test_view_blob_equiv.cpp)
    # Inc-1 M4: must whole-archive VixenApp, not link it plainly -- see cmake/VixenNodeLinkage.cmake.
    vixen_whole_archive_link_vixen_app(test_view_blob_equiv PRIVATE)
    target_link_libraries(test_view_blob_equiv PRIVATE GTest::gtest_main)
    target_include_directories(test_view_blob_equiv PRIVATE ${VIXEN_ROOT}/libraries/Core/include)

    # The test resolves "assets/ui/hud.viewblob" (ViewBlobFile::Load) relative to its CWD, matching
    # how test_ui_hud_smoke resolves hud.rml/font paths -- stage the same UI assets next to this
    # test's binary via the shared vixen_stage_assets helper (cmake/VixenAssets.cmake).
    vixen_stage_assets(test_view_blob_equiv ${VIXEN_ROOT}/libraries/RenderGraph/assets DEST assets)

    set_target_properties(test_view_blob_equiv PROPERTIES FOLDER "Tests/Application")
    gtest_discover_tests(test_view_blob_equiv)
    message(STATUS "[Application Tests] Added: test_view_blob_equiv (View Contract Inc-2b: native==header==datafile proof gate)")
endif()
