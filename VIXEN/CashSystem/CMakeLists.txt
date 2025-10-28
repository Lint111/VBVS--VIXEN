cmake_minimum_required(VERSION 3.16)
project(CashSystem)

add_library(CashSystem STATIC
    src/main_cacher.cpp
    src/pipeline_cacher.cpp
)

target_include_directories(CashSystem PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Link dependencies here if/when required
target_link_libraries(CashSystem PUBLIC
)
