#pragma once

#include <string>
#include <stdexcept>
#include <vulkan/vulkan.h>

namespace RenderGraph::NodeHelpers {

/// Parse Vulkan cull mode from string.
/// Valid: "None", "Front", "Back", "FrontAndBack"
inline VkCullModeFlagBits ParseCullMode(const std::string& mode) {
    if (mode == "None") return VK_CULL_MODE_NONE;
    if (mode == "Front") return VK_CULL_MODE_FRONT_BIT;
    if (mode == "Back") return VK_CULL_MODE_BACK_BIT;
    if (mode == "FrontAndBack") return VK_CULL_MODE_FRONT_AND_BACK;
    throw std::runtime_error("Unknown cull mode: " + mode);
}

/// Parse Vulkan polygon mode from string.
/// Valid: "Fill", "Line", "Point"
inline VkPolygonMode ParsePolygonMode(const std::string& mode) {
    if (mode == "Fill") return VK_POLYGON_MODE_FILL;
    if (mode == "Line") return VK_POLYGON_MODE_LINE;
    if (mode == "Point") return VK_POLYGON_MODE_POINT;
    throw std::runtime_error("Unknown polygon mode: " + mode);
}

/// Parse Vulkan primitive topology from string.
/// Valid: "PointList", "LineList", "LineStrip", "TriangleList", "TriangleStrip"
inline VkPrimitiveTopology ParseTopology(const std::string& topology) {
    if (topology == "PointList") return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    if (topology == "LineList") return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    if (topology == "LineStrip") return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    if (topology == "TriangleList") return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    if (topology == "TriangleStrip") return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    throw std::runtime_error("Unknown topology: " + topology);
}

/// Parse Vulkan front face from string.
/// Valid: "CounterClockwise", "Clockwise"
inline VkFrontFace ParseFrontFace(const std::string& face) {
    if (face == "CounterClockwise") return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    if (face == "Clockwise") return VK_FRONT_FACE_CLOCKWISE;
    throw std::runtime_error("Unknown front face: " + face);
}

/// Parse Vulkan image layout from string.
/// Valid: "Undefined", "General", "ColorAttachmentOptimal", "DepthStencilAttachmentOptimal",
///        "DepthStencilReadOnlyOptimal", "ShaderReadOnlyOptimal", "TransferSrcOptimal",
///        "TransferDstOptimal", "Preinitialized", "PresentSrc"
inline VkImageLayout ParseImageLayout(const std::string& layout) {
    if (layout == "Undefined") return VK_IMAGE_LAYOUT_UNDEFINED;
    if (layout == "General") return VK_IMAGE_LAYOUT_GENERAL;
    if (layout == "ColorAttachmentOptimal") return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    if (layout == "DepthStencilAttachmentOptimal") return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    if (layout == "DepthStencilReadOnlyOptimal") return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    if (layout == "ShaderReadOnlyOptimal") return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (layout == "TransferSrcOptimal") return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    if (layout == "TransferDstOptimal") return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    if (layout == "Preinitialized") return VK_IMAGE_LAYOUT_PREINITIALIZED;
    if (layout == "PresentSrc") return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    throw std::runtime_error("Unknown image layout: " + layout);
}

/// Parse Vulkan attachment load op from string.
/// Valid: "Load", "Clear", "DontCare"
inline VkAttachmentLoadOp ParseAttachmentLoadOp(const std::string& op) {
    if (op == "Load") return VK_ATTACHMENT_LOAD_OP_LOAD;
    if (op == "Clear") return VK_ATTACHMENT_LOAD_OP_CLEAR;
    if (op == "DontCare") return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    throw std::runtime_error("Unknown attachment load op: " + op);
}

/// Parse Vulkan attachment store op from string.
/// Valid: "Store", "DontCare"
inline VkAttachmentStoreOp ParseAttachmentStoreOp(const std::string& op) {
    if (op == "Store") return VK_ATTACHMENT_STORE_OP_STORE;
    if (op == "DontCare") return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    throw std::runtime_error("Unknown attachment store op: " + op);
}

/// Parse Vulkan compare op from string.
/// Valid: "Never", "Less", "Equal", "LessOrEqual", "Greater", "NotEqual", "GreaterOrEqual", "Always"
inline VkCompareOp ParseCompareOp(const std::string& op) {
    if (op == "Never") return VK_COMPARE_OP_NEVER;
    if (op == "Less") return VK_COMPARE_OP_LESS;
    if (op == "Equal") return VK_COMPARE_OP_EQUAL;
    if (op == "LessOrEqual") return VK_COMPARE_OP_LESS_OR_EQUAL;
    if (op == "Greater") return VK_COMPARE_OP_GREATER;
    if (op == "NotEqual") return VK_COMPARE_OP_NOT_EQUAL;
    if (op == "GreaterOrEqual") return VK_COMPARE_OP_GREATER_OR_EQUAL;
    if (op == "Always") return VK_COMPARE_OP_ALWAYS;
    throw std::runtime_error("Unknown compare op: " + op);
}

/// Parse sample count from integer.
/// Valid: 1, 2, 4, 8, 16, 32, 64
inline VkSampleCountFlagBits ParseSampleCount(uint32_t samples) {
    switch (samples) {
        case 1: return VK_SAMPLE_COUNT_1_BIT;
        case 2: return VK_SAMPLE_COUNT_2_BIT;
        case 4: return VK_SAMPLE_COUNT_4_BIT;
        case 8: return VK_SAMPLE_COUNT_8_BIT;
        case 16: return VK_SAMPLE_COUNT_16_BIT;
        case 32: return VK_SAMPLE_COUNT_32_BIT;
        case 64: return VK_SAMPLE_COUNT_64_BIT;
        default: throw std::runtime_error("Invalid sample count: " + std::to_string(samples));
    }
}

} // namespace RenderGraph::NodeHelpers
