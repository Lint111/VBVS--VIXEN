# GaiaArchetypes Tests

add_executable(test_gaia_archetypes
    test_archetype_builder.cpp
    test_relationship_observer.cpp
    test_voxel_volume_archetype.cpp
)

target_link_libraries(test_gaia_archetypes
    PRIVATE
        GaiaArchetypes
        GTest::gtest_main
)

# Organize in Tests folder
set_target_properties(test_gaia_archetypes PROPERTIES FOLDER "Tests")

include(GoogleTest)
gtest_discover_tests(test_gaia_archetypes)
