// ============================================================================
// Vulkan Coordinate System Test
// ============================================================================
// Tests GLM projection and view matrices for Vulkan compatibility
// Verifies: Y-axis orientation, depth range [0,1], right-handed system
// Expected: X+ right, Y+ DOWN (clip space), Z+ forward (into screen), depth [0,1]

#include <iostream>
#include <iomanip>

// GLM Configuration for Vulkan
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE  // Critical for Vulkan [0,1] depth
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// ============================================================================
// Utility Functions
// ============================================================================

void PrintVec3(const char* label, const glm::vec3& v) {
    std::cout << label << " = ("
              << std::fixed << std::setprecision(4)
              << v.x << ", " << v.y << ", " << v.z << ")" << std::endl;
}

void PrintVec4(const char* label, const glm::vec4& v) {
    std::cout << label << " = ("
              << std::fixed << std::setprecision(4)
              << v.x << ", " << v.y << ", " << v.z << ", " << v.w << ")" << std::endl;
}

void PrintMat4(const char* label, const glm::mat4& m) {
    std::cout << label << ":" << std::endl;
    for (int row = 0; row < 4; ++row) {
        std::cout << "  [";
        for (int col = 0; col < 4; ++col) {
            std::cout << std::fixed << std::setprecision(4) << std::setw(9) << m[col][row];
            if (col < 3) std::cout << ", ";
        }
        std::cout << "]" << std::endl;
    }
}

glm::vec4 TransformPoint(const glm::mat4& mvp, const glm::vec3& point) {
    glm::vec4 clip = mvp * glm::vec4(point, 1.0f);
    return clip;
}

glm::vec3 ClipToNDC(const glm::vec4& clip) {
    // Perspective divide
    if (clip.w == 0.0f) {
        return glm::vec3(0.0f);
    }
    return glm::vec3(clip.x / clip.w, clip.y / clip.w, clip.z / clip.w);
}

// ============================================================================
// Test Functions
// ============================================================================

void TestCameraVectors() {
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "TEST 1: Camera Basis Vectors (View Space Convention)" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    // Camera looking down -Z axis (standard OpenGL/GLM convention)
    glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f, 3.0f);
    glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);  // Look at origin
    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);  // Y+ is world up

    // Create view matrix
    glm::mat4 view = glm::lookAt(cameraPos, target, worldUp);

    // Extract camera basis vectors from view matrix
    // View matrix transforms world to view space, so we need inverse for camera orientation
    glm::mat4 invView = glm::inverse(view);
    glm::vec3 right = glm::vec3(invView[0]);
    glm::vec3 up = glm::vec3(invView[1]);
    glm::vec3 forward = -glm::vec3(invView[2]);  // Note: -Z is forward in view space

    std::cout << "\nCamera at origin looking toward -Z:" << std::endl;
    PrintVec3("  Camera Position", cameraPos);
    PrintVec3("  Camera Forward (toward -Z)", forward);
    PrintVec3("  Camera Right (X+)", right);
    PrintVec3("  Camera Up (Y+)", up);

    // Verify right-handed system: right x up = forward
    glm::vec3 crossProduct = glm::cross(right, up);
    PrintVec3("  Cross(right, up)", crossProduct);
    bool isRightHanded = glm::dot(crossProduct, forward) > 0.99f;
    std::cout << "  Right-handed system: " << (isRightHanded ? "YES ✓" : "NO ✗") << std::endl;
}

void TestProjectionMatrix() {
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "TEST 2: Projection Matrix Properties" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    float fov = 45.0f;
    float aspect = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;

    glm::mat4 projection = glm::perspective(
        glm::radians(fov),
        aspect,
        nearPlane,
        farPlane
    );

    std::cout << "\nProjection parameters:" << std::endl;
    std::cout << "  FOV: " << fov << " degrees" << std::endl;
    std::cout << "  Aspect: " << aspect << std::endl;
    std::cout << "  Near: " << nearPlane << std::endl;
    std::cout << "  Far: " << farPlane << std::endl;

    PrintMat4("\nProjection matrix", projection);

    // Test depth range: transform points at near and far planes
    glm::vec4 nearPoint = projection * glm::vec4(0.0f, 0.0f, -nearPlane, 1.0f);
    glm::vec4 farPoint = projection * glm::vec4(0.0f, 0.0f, -farPlane, 1.0f);

    float nearDepth = nearPoint.z / nearPoint.w;
    float farDepth = farPoint.z / farPoint.w;

    std::cout << "\nDepth range test:" << std::endl;
    std::cout << "  Near plane NDC Z: " << std::fixed << std::setprecision(6) << nearDepth << std::endl;
    std::cout << "  Far plane NDC Z: " << std::fixed << std::setprecision(6) << farDepth << std::endl;
    std::cout << "  Depth range [0,1]: " << ((nearDepth >= 0.0f && farDepth <= 1.0f) ? "YES ✓" : "NO ✗") << std::endl;

    // Check if Y is flipped (projection[1][1] should be negative for Vulkan)
    bool yFlipped = projection[1][1] < 0.0f;
    std::cout << "  Y-axis inverted (projection[1][1] < 0): " << (yFlipped ? "YES ✓" : "NO ✗") << std::endl;
}

void TestClipSpaceTransform() {
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "TEST 3: World to Clip Space Transform" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    // Setup camera and projection
    glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f, 5.0f);
    glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

    glm::mat4 view = glm::lookAt(cameraPos, target, worldUp);
    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f),
        16.0f / 9.0f,
        0.1f,
        100.0f
    );

    glm::mat4 mvp = projection * view;

    // Test points
    struct TestPoint {
        const char* name;
        glm::vec3 worldPos;
        const char* expectedResult;
    };

    TestPoint testPoints[] = {
        {"Origin (0,0,0)", glm::vec3(0.0f, 0.0f, 0.0f), "Should be at center, mid-depth"},
        {"Right (+X)", glm::vec3(1.0f, 0.0f, 0.0f), "Should be right of center"},
        {"Left (-X)", glm::vec3(-1.0f, 0.0f, 0.0f), "Should be left of center"},
        {"Up (+Y)", glm::vec3(0.0f, 1.0f, 0.0f), "Should be DOWN in clip space (Y inverted)"},
        {"Down (-Y)", glm::vec3(0.0f, -1.0f, 0.0f), "Should be UP in clip space (Y inverted)"},
        {"Near (camera - 1)", glm::vec3(0.0f, 0.0f, 4.0f), "Should have depth near 0"},
        {"Far (camera - 90)", glm::vec3(0.0f, 0.0f, -85.0f), "Should have depth near 1"},
    };

    for (const auto& test : testPoints) {
        std::cout << "\n" << test.name << ":" << std::endl;
        PrintVec3("  World position", test.worldPos);

        glm::vec4 clip = TransformPoint(mvp, test.worldPos);
        PrintVec4("  Clip space", clip);

        glm::vec3 ndc = ClipToNDC(clip);
        PrintVec3("  NDC", ndc);

        std::cout << "  Expected: " << test.expectedResult << std::endl;
    }
}

void TestYawPitchToVector() {
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "TEST 4: Yaw/Pitch to Direction Vector" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    struct YawPitchTest {
        const char* name;
        float yaw;     // Radians
        float pitch;   // Radians
        const char* expected;
    };

    YawPitchTest tests[] = {
        {"Forward (-Z)", 0.0f, 0.0f, "Should look toward -Z"},
        {"Right (+X)", glm::radians(90.0f), 0.0f, "Should look toward +X"},
        {"Left (-X)", glm::radians(-90.0f), 0.0f, "Should look toward -X"},
        {"Up (+Y)", 0.0f, glm::radians(90.0f), "Should look toward +Y"},
        {"Down (-Y)", 0.0f, glm::radians(-90.0f), "Should look toward -Y"},
    };

    std::cout << "\nYaw/Pitch convention (from CameraNode.cpp):" << std::endl;
    std::cout << "  forward.x = cos(pitch) * sin(yaw)" << std::endl;
    std::cout << "  forward.y = sin(pitch)" << std::endl;
    std::cout << "  forward.z = -cos(pitch) * cos(yaw)  // Note: -Z is forward" << std::endl;

    for (const auto& test : tests) {
        glm::vec3 forward;
        forward.x = cos(test.pitch) * sin(test.yaw);
        forward.y = sin(test.pitch);
        forward.z = -cos(test.pitch) * cos(test.yaw);
        forward = glm::normalize(forward);

        std::cout << "\n" << test.name << ":" << std::endl;
        std::cout << "  Yaw: " << glm::degrees(test.yaw) << "°, Pitch: " << glm::degrees(test.pitch) << "°" << std::endl;
        PrintVec3("  Forward vector", forward);
        std::cout << "  Expected: " << test.expected << std::endl;
    }
}

void TestDepthPrecision() {
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "TEST 5: Depth Buffer Precision" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f),
        16.0f / 9.0f,
        nearPlane,
        farPlane
    );

    std::cout << "\nDepth distribution for near=" << nearPlane << ", far=" << farPlane << ":" << std::endl;
    std::cout << std::setw(15) << "View Z"
              << std::setw(15) << "NDC Z [0,1]"
              << std::setw(20) << "Precision (ΔZ)" << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    float testDistances[] = {0.1f, 0.5f, 1.0f, 5.0f, 10.0f, 50.0f, 100.0f, 500.0f, 1000.0f};

    float prevDepth = 0.0f;
    for (float dist : testDistances) {
        glm::vec4 clip = projection * glm::vec4(0.0f, 0.0f, -dist, 1.0f);
        float depth = clip.z / clip.w;
        float precision = depth - prevDepth;

        std::cout << std::setw(15) << std::fixed << std::setprecision(2) << dist
                  << std::setw(15) << std::setprecision(6) << depth
                  << std::setw(20) << std::setprecision(6) << precision << std::endl;

        prevDepth = depth;
    }

    std::cout << "\nNote: Most precision near 0 (near plane), less precision near 1 (far plane)" << std::endl;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "╔═══════════════════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║            VULKAN COORDINATE SYSTEM TEST - GLM Configuration             ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════════════════════════════════════════╝" << std::endl;

    std::cout << "\nGLM Configuration:" << std::endl;
    std::cout << "  GLM_FORCE_RADIANS: " <<
#ifdef GLM_FORCE_RADIANS
        "ENABLED ✓"
#else
        "DISABLED ✗"
#endif
        << std::endl;

    std::cout << "  GLM_FORCE_DEPTH_ZERO_TO_ONE: " <<
#ifdef GLM_FORCE_DEPTH_ZERO_TO_ONE
        "ENABLED ✓"
#else
        "DISABLED ✗"
#endif
        << std::endl;

    std::cout << "\nVulkan NDC Space:" << std::endl;
    std::cout << "  X: -1 (left) to +1 (right)" << std::endl;
    std::cout << "  Y: -1 (top) to +1 (bottom) - INVERTED from world space" << std::endl;
    std::cout << "  Z: 0 (near) to 1 (far) - depth increases into screen" << std::endl;
    std::cout << "  Right-handed coordinate system" << std::endl;

    // Run all tests
    TestCameraVectors();
    TestProjectionMatrix();
    TestClipSpaceTransform();
    TestYawPitchToVector();
    TestDepthPrecision();

    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "All tests complete!" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    return 0;
}
