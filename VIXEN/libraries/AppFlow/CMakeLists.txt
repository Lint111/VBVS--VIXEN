cmake_minimum_required(VERSION 3.20...4.2)
project(AppFlow VERSION 1.0.0 LANGUAGES CXX)

file(GLOB APPFLOW_GENERATED_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/generated/*.g.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/generated/*.generated.g.hpp")

set(APPFLOW_HEADERS
    ${APPFLOW_GENERATED_HEADERS}
    include/AppFlowEvents.h
    include/AppFlowResults.h
    include/AppFlowLoader.h
    include/ActionStack.h
    include/FlowStateMachine.h
    include/BindingStore.h
    include/LayerController.h
    include/AppFlowRuntime.h
    include/InputProfile.h
    include/IViewDataProvider.h
    include/LayerControllerViewDataProvider.h
    include/IViewSelectionProvider.h
    include/DirectListViewSelectionProvider.h
)
set(APPFLOW_SOURCES
    src/AppFlowLoader.cpp
    src/ActionStack.cpp
    src/FlowStateMachine.cpp
    src/BindingStore.cpp
    src/LayerController.cpp
    src/AppFlowRuntime.cpp
    src/InputProfile.cpp
)
add_library(AppFlow STATIC ${APPFLOW_HEADERS} ${APPFLOW_SOURCES})
target_include_directories(AppFlow PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(AppFlow PUBLIC cxx_std_23)
set_target_properties(AppFlow PROPERTIES FOLDER "Libraries")
target_link_libraries(AppFlow PUBLIC EventBus)   # MessageBus + BaseEventMessage

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
