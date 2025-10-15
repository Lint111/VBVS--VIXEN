#pragma once

#ifdef _WIN32
#pragma comment(linker, "/subsystem:console")
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define APP_NAME_STR_LEN 80
#define _CRT_SECURE_NO_WARNINGS
#else // _WIN32
#define  VK_USE_PLATFORM_XCB_KHR
#include <unistd.h>
#endif // _WIN32

#define VK_Enable_Beta_Extensions
#include <vulkan/vulkan.h>
#ifdef AUTO_COMPILE_GLSL_TO_SPV
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#endif // AUTO_COMPILE_GLSL_TO_SPV

// GLM Header files
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <iomanip>
#include <assert.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// error handling
#include "error/VulkanError.h"

#include <memory>
#include <mutex>
#include <chrono>