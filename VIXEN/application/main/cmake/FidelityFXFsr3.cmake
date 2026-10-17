# Build the pinned FidelityFX SDK 1.1.4 FSR3 upscaler and its Vulkan shader blobs.
set(FFX_SDK_ROOT "${VIXEN_ROOT}/third_party/fidelityfx-sdk-1.1.4/sdk")
set(FFX_GPU_PATH "${FFX_SDK_ROOT}/include/FidelityFX/gpu")
set(FFX_SHADER_TOOL_DIR "${FFX_SDK_ROOT}/tools/binary_store")
set(FFX_SHADER_COMPILER "${FFX_SHADER_TOOL_DIR}/FidelityFX_SC.exe")
set(FFX_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/fidelityfx-fsr3")
file(MAKE_DIRECTORY "${FFX_SHADER_OUTPUT_DIR}")

if(WIN32)
    set(FFX_TOOL_PATH "${FFX_SHADER_TOOL_DIR};$ENV{PATH}")
else()
    set(FFX_TOOL_PATH "${FFX_SHADER_TOOL_DIR}:$ENV{PATH}")
endif()

set(FFX_FSR3_SHADER_BASE_ARGS
    -compiler=glslang -e CS --target-env vulkan1.2 -S comp -Os -DFFX_GLSL=1
    -reflection -DFFX_GPU=1
    -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0
    -DFFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0
    -DFFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1
    -DFFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0
    -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2)
set(FFX_FSR3_SHADER_PERMUTATION_ARGS
    "-DFFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE={0,1}"
    "-DFFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT={0,1}"
    "-DFFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS={0,1}"
    "-DFFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS={0,1}"
    "-DFFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH={0,1}"
    "-DFFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING={0,1}")
set(FFX_FSR3_SHADER_SOURCES
    "${FFX_SDK_ROOT}/src/backends/vk/shaders/fsr3upscaler/ffx_fsr3upscaler_accumulate_pass.glsl"
    "${FFX_SDK_ROOT}/src/backends/vk/shaders/fsr3upscaler/ffx_fsr3upscaler_autogen_reactive_pass.glsl"
    "${FFX_SDK_ROOT}/src/backends/vk/shaders/fsr3upscaler/ffx_fsr3upscaler_debug_view_pass.glsl"
    "${FFX_SDK_ROOT}/src/backends/vk/shaders/fsr3upscaler/ffx_fsr3upscaler_luma_instability_pass.glsl"
    "${FFX_SDK_ROOT}/src/backends/vk/shaders/fsr3upscaler/ffx_fsr3upscaler_luma_pyramid_pass.glsl"
    "${FFX_SDK_ROOT}/src/backends/vk/shaders/fsr3upscaler/ffx_fsr3upscaler_prepare_inputs_pass.glsl"
    "${FFX_SDK_ROOT}/src/backends/vk/shaders/fsr3upscaler/ffx_fsr3upscaler_prepare_reactivity_pass.glsl"
    "${FFX_SDK_ROOT}/src/backends/vk/shaders/fsr3upscaler/ffx_fsr3upscaler_rcas_pass.glsl"
    "${FFX_SDK_ROOT}/src/backends/vk/shaders/fsr3upscaler/ffx_fsr3upscaler_shading_change_pass.glsl"
    "${FFX_SDK_ROOT}/src/backends/vk/shaders/fsr3upscaler/ffx_fsr3upscaler_shading_change_pyramid_pass.glsl")
file(GLOB_RECURSE FFX_FSR3_SHADER_DEPENDENCIES CONFIGURE_DEPENDS
    "${FFX_GPU_PATH}/*.h"
    "${FFX_GPU_PATH}/*.glsl")

set(FFX_FSR3_SHADER_OUTPUTS)
foreach(shader IN LISTS FFX_FSR3_SHADER_SOURCES)
    get_filename_component(shader_name "${shader}" NAME_WE)
    foreach(variant IN ITEMS wave32 wave64 wave32_16bit wave64_16bit)
        if(variant STREQUAL "wave32")
            set(shader_suffix "")
            set(half_flag 0)
        elseif(variant STREQUAL "wave64")
            set(shader_suffix "_wave64")
            set(half_flag 0)
        elseif(variant STREQUAL "wave32_16bit")
            set(shader_suffix "_16bit")
            set(half_flag 1)
        else()
            set(shader_suffix "_wave64_16bit")
            set(half_flag 1)
        endif()
        set(shader_output "${FFX_SHADER_OUTPUT_DIR}/${shader_name}${shader_suffix}_permutations.h")
        add_custom_command(
            OUTPUT "${shader_output}"
            COMMAND "${CMAKE_COMMAND}" -E env "PATH=${FFX_TOOL_PATH}"
                "${FFX_SHADER_COMPILER}" ${FFX_FSR3_SHADER_BASE_ARGS}
                ${FFX_FSR3_SHADER_PERMUTATION_ARGS}
                "-I${FFX_GPU_PATH}" "-I${FFX_GPU_PATH}/fsr3upscaler"
                "-name=${shader_name}${shader_suffix}" "-DFFX_HALF=${half_flag}"
                "-output=${FFX_SHADER_OUTPUT_DIR}" "${shader}"
            DEPENDS "${shader}" "${FFX_SHADER_COMPILER}"
                "${FFX_SHADER_TOOL_DIR}/glslangValidator.exe"
                ${FFX_FSR3_SHADER_DEPENDENCIES}
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            VERBATIM)
        list(APPEND FFX_FSR3_SHADER_OUTPUTS "${shader_output}")
    endforeach()
endforeach()

add_custom_target(FidelityFXFsr3ShaderBlobs DEPENDS ${FFX_FSR3_SHADER_OUTPUTS})

set(FFX_SHARED_ROOT "${FFX_SDK_ROOT}/src/shared")
set(FFX_BACKEND_ROOT "${FFX_SDK_ROOT}/src/backends")
set(FFX_BACKEND_SHARED "${FFX_BACKEND_ROOT}/shared")
set(FFX_COMPONENT_ROOT "${FFX_SDK_ROOT}/src/components/fsr3upscaler")
add_library(FidelityFXFsr3VK STATIC
    "${FFX_SHARED_ROOT}/ffx_assert.cpp"
    "${FFX_SHARED_ROOT}/ffx_breadcrumbs_list.cpp"
    "${FFX_SHARED_ROOT}/ffx_message.cpp"
    "${FFX_SHARED_ROOT}/ffx_object_management.cpp"
    "${FFX_BACKEND_SHARED}/ffx_shader_blobs.cpp"
    "${FFX_BACKEND_SHARED}/blob_accessors/ffx_fsr3upscaler_shaderblobs.cpp"
    "${FFX_BACKEND_ROOT}/vk/ffx_vk.cpp"
    "${FFX_COMPONENT_ROOT}/ffx_fsr3upscaler.cpp"
    "${VIXEN_ROOT}/application/main/source/graph/Fsr3UpscalerOnlyVK.cpp"
    ${FFX_FSR3_SHADER_OUTPUTS})
add_dependencies(FidelityFXFsr3VK FidelityFXFsr3ShaderBlobs)
target_compile_features(FidelityFXFsr3VK PUBLIC cxx_std_17)
target_compile_definitions(FidelityFXFsr3VK PUBLIC FFX_SDK_DEFAULT_CONTEXT_SIZE=1048576)
target_compile_definitions(FidelityFXFsr3VK PRIVATE FFX_FSR3UPSCALER)
if(NOT WIN32)
    target_compile_options(FidelityFXFsr3VK PRIVATE
        "-include" "${CMAKE_CURRENT_LIST_DIR}/FidelityFXFsr3LinuxCompat.h")
endif()
target_include_directories(FidelityFXFsr3VK
    PUBLIC "${FFX_SDK_ROOT}/include"
    PRIVATE "${FFX_GPU_PATH}"
        "${FFX_SHARED_ROOT}"
        "${FFX_BACKEND_SHARED}"
        "${FFX_BACKEND_SHARED}/blob_accessors"
        "${FFX_BACKEND_ROOT}/vk"
        "${FFX_SDK_ROOT}/src/components"
        "${FFX_COMPONENT_ROOT}"
        "${FFX_SHADER_OUTPUT_DIR}")
target_link_libraries(FidelityFXFsr3VK PUBLIC ${VULKAN_LIB_LIST})
set_target_properties(FidelityFXFsr3VK PROPERTIES FOLDER "ThirdParty/FidelityFX")
