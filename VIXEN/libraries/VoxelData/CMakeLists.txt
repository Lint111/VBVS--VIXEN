cmake_minimum_required(VERSION 3.20)
project(VoxelData VERSION 1.0.0 LANGUAGES CXX)

# Library sources
set(VOXELDATA_SOURCES
    src/AttributeRegistry.cpp
    src/AttributeStorage.cpp
    src/BrickView.cpp
    src/DynamicVoxelStruct.cpp
    src/Compression/BlockCompressor.cpp
    src/Compression/DXT1Compressor.cpp
)

# Library headers
set(VOXELDATA_HEADERS
    include/VoxelDataTypes.h
    include/ArrayView.h
    include/AttributeStorage.h
    include/AttributeRegistry.h
    include/BrickView.h
    include/VoxelConfig.h
    include/StandardVoxelConfigs.h
    include/DynamicVoxelStruct.h
    include/Compression/BlockCompressor.h
    include/Compression/DXT1Compressor.h
)

# Create static library
add_library(VoxelData STATIC ${VOXELDATA_SOURCES} ${VOXELDATA_HEADERS})

# C++23 required for fold expressions and template features
target_compile_features(VoxelData PUBLIC cxx_std_23)

# Include directories
target_include_directories(VoxelData
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Link dependencies
target_link_libraries(VoxelData
    PUBLIC
        VoxelComponents  # Component definitions (Gaia + GLM included transitively)
)

# Platform-specific settings
if(MSVC)
    target_compile_options(VoxelData PRIVATE /W4)
else()
    target_compile_options(VoxelData PRIVATE -Wall -Wextra -pedantic)
endif()

# Export for use by other libraries
set_target_properties(VoxelData PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    EXPORT_NAME VoxelData
)

# Installation rules (optional)
install(TARGETS VoxelData
    EXPORT VoxelDataTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)
