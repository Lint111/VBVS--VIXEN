#version 450
// RmlUi 2D UI vertex shader. Maps RmlUi's top-left pixel space to Vulkan NDC (y-down, no flip).
// Vertex layout matches Rml::Vertex { vec2 position; Colourb colour; vec2 tex_coord; }.
layout(location = 0) in vec2 inPos;     // pixels, top-left origin
layout(location = 1) in vec4 inColour;  // R8G8B8A8_UNORM (premultiplied-alpha sRGB)
layout(location = 2) in vec2 inUV;

layout(push_constant) uniform PushConstants {
    vec2 translation;  // per-RenderGeometry() call, in pixels
    vec2 viewport;     // context dimensions in pixels
} pc;

layout(location = 0) out vec4 vColour;
layout(location = 1) out vec2 vUV;

void main() {
    vec2 p = inPos + pc.translation;
    vec2 ndc = vec2(2.0 * p.x / pc.viewport.x - 1.0,
                    2.0 * p.y / pc.viewport.y - 1.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColour = inColour;
    vUV = inUV;
}
