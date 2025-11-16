cmake_minimum_required(VERSION 3.20)

project(EventBus VERSION 1.0.0 LANGUAGES CXX)

# ============================================================================
# EventBus Static Library
# ============================================================================

set(EVENTBUS_HEADERS
    include/EventBus/Message.h
    include/EventBus/MessageBus.h
    include/EventBus/WorkerThreadBridge.h
)

set(EVENTBUS_SOURCES
    src/MessageBus.cpp
)

add_library(EventBus STATIC
    ${EVENTBUS_HEADERS}
    ${EVENTBUS_SOURCES}
)

target_include_directories(EventBus
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(EventBus PUBLIC cxx_std_23)

# Thread support required for std::thread
find_package(Threads REQUIRED)
target_link_libraries(EventBus PUBLIC Threads::Threads)

# ============================================================================
# IDE Support - Group files
# ============================================================================

source_group("Header Files" FILES ${EVENTBUS_HEADERS})
source_group("Source Files" FILES ${EVENTBUS_SOURCES})

# ============================================================================
# Installation (optional)
# ============================================================================

install(TARGETS EventBus
    EXPORT EventBusTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/EventBus
    DESTINATION include
)

# ============================================================================
# Tests
# ============================================================================

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
