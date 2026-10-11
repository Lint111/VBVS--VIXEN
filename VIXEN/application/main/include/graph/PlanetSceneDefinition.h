#pragma once

#include "Recipe/RecipeRegistry.h"
#include "ShellOctreeGpu.h"
#include "Generated/LightingConfig.g.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Vixen::App::Planet {

// Planet-and-sun reference scene (planetref.md section 3). Every length is in metres.
// Positions and circular orbits are evaluated in double in a sun-centred sim frame;
// each capture state uploads camera-relative float transforms (section 1.5), so the
// camera sits at the render-space origin and no float ever holds an absolute position.
//
// Sim frame: +X is the epoch sun direction seen from the planet, +Y is planet north,
// the planet orbits the sun in the XZ plane.

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kDegrees = kPi / 180.0;

inline constexpr double kAstronomicalUnit = 1.495978707e11;
inline constexpr double kSunRadius = 6.957e8;
inline constexpr double kPlanetRadius = 6.371e6;
inline constexpr double kMoonRadius = 1.737e6;
inline constexpr double kPlanetPeriod = 365.25 * 86400.0;
inline constexpr double kMoonOrbitRadius = 3.844e8;
inline constexpr double kMoonPeriod = 27.32 * 86400.0;
inline constexpr double kMoonInclination = 5.1 * kDegrees;
inline constexpr double kStationAltitude = 4.0e5;
inline constexpr double kStationOrbitRadius = kPlanetRadius + kStationAltitude;
inline constexpr double kStationPeriod = 92.6 * 60.0;
inline constexpr double kStationInclination = 51.6 * kDegrees;
inline constexpr double kStationHalfLength = 50.0;   // capsule tip to centre; hull is 100 m long
inline constexpr double kProbeRadius = 0.5;
inline constexpr double kProbeLead = 2000.0;
inline constexpr double kPatchCameraHeight = 1.7;
inline constexpr double kPatchSunElevation = 14.0 * kDegrees;
inline constexpr float kSunLightRadiance = 1.0f;     // 1.0 = solar irradiance at 1 AU (planetref D7)
// The 1x1 exposure meter reads the top-left 16x16 tile only. Over the hard-coded sky that tile
// meters 5.453984 EV; compensating by -meter plus the target puts a white diffuse surface in full
// sun (radiance 1/pi) near display white.
inline constexpr double kSkyCornerEv100 = 5.453984;
inline constexpr double kExposureTargetEV = 1.585;    // exposure 3
inline float ExposureFor(double meterEv100) { return static_cast<float>(kExposureTargetEV - meterEv100); }
inline constexpr float kSunEmission = 1.5e4f;        // scene-unit radiance, planetref 3.1
inline constexpr float kStationLampEmission = 6.0f;
inline constexpr float kPlanetAlbedo = 0.30f;
inline constexpr float kMoonAlbedo = 0.12f;
inline constexpr float kProbeAlbedo = 1.0f;
inline constexpr uint32_t kCaptureExtent = 1080;
// States that look at the planet from orbit or through the station exceed the ~2 s Dozen (D3D12 TDR)
// submit limit at 1080 square; 900 square was measured to lose the device, 720 square to pass.
inline constexpr uint32_t kHeavyCaptureExtent = 720;
// The 150-lamp lattice (157 instances) loses the device on Dozen at 256 square and passes at 128 square.
inline constexpr uint32_t kLampLatticeExtent = 128;
inline constexpr size_t kMaxStateInstances = 192;

enum class RecipeSlot : uint32_t {
    UnitSphere = 200,
    StationHull = 201,
    StationAccent = 202,
    StationPanels = 203,
    TerrainGround = 204,
    TerrainRocks = 205,
};

struct NamedRecipe {
    RecipeSlot slot;
    const char* name;
    Vixen::SVO::RecipeRegistry::RecipeEntry entry;
};

inline Vixen::SVO::Recipe::SdfInstruction SphereOp(const glm::vec3& center, float radius) {
    Vixen::SVO::Recipe::SdfInstruction op{};
    op.opCode = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Sphere);
    op.data[0] = center.x;
    op.data[1] = center.y;
    op.data[2] = center.z;
    op.data[3] = radius;
    return op;
}

inline Vixen::SVO::Recipe::SdfInstruction EllipsoidOp(const glm::vec3& center, const glm::vec3& radii) {
    Vixen::SVO::Recipe::SdfInstruction op{};
    op.opCode = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Ellipsoid);
    op.data[0] = radii.x;
    op.data[1] = radii.y;
    op.data[2] = radii.z;
    op.data[4] = center.x;
    op.data[5] = center.y;
    op.data[6] = center.z;
    return op;
}

inline Vixen::SVO::Recipe::SdfInstruction CapsuleOp(float halfSegment, float radius) {
    Vixen::SVO::Recipe::SdfInstruction op{};
    op.opCode = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Capsule);
    op.data[0] = halfSegment;
    op.data[1] = radius;
    return op;
}

inline Vixen::SVO::Recipe::SdfInstruction BoxOp(const glm::vec3& center, const glm::vec3& half, float round) {
    Vixen::SVO::Recipe::SdfInstruction op{};
    op.opCode = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::RoundedBox);
    op.data[0] = half.x;
    op.data[1] = half.y;
    op.data[2] = half.z;
    op.data[3] = round;
    op.data[4] = center.x;
    op.data[5] = center.y;
    op.data[6] = center.z;
    return op;
}

inline Vixen::SVO::Recipe::SdfInstruction UnionOp() {
    Vixen::SVO::Recipe::SdfInstruction op{};
    op.opCode = static_cast<uint8_t>(Vixen::SVO::Recipe::SdfOpCode::Union);
    return op;
}

inline Vixen::SVO::RecipeRegistry::RecipeEntry UnionOf(
    const std::vector<Vixen::SVO::Recipe::SdfInstruction>& primitives,
    const glm::vec3& boundCenter, float boundRadius) {
    Vixen::SVO::RecipeRegistry::RecipeEntry entry{};
    entry.boundCenter = boundCenter;
    entry.boundRadius = boundRadius;
    entry.stepRelaxation = 0.9f;
    for (size_t i = 0; i < primitives.size(); ++i) {
        entry.bytecode.push_back(primitives[i]);
        if (i > 0) entry.bytecode.push_back(UnionOp());
    }
    return entry;
}

// Deterministic pseudo-random rocks for the near-surface patch (fixed LCG, no <random>).
struct RockPlacement {
    glm::vec3 center;
    glm::vec3 radii;
};

inline std::vector<RockPlacement> PatchRocks() {
    std::vector<RockPlacement> rocks;
    uint32_t state = 0x1d872b41u;
    auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
    };
    // Foreground boulders at hand-picked ranges so the long dawn shadows cross the frame.
    const std::array<RockPlacement, 6> boulders{{
        {{ -3.5f, -kPatchCameraHeight + 0.35f, -9.0f}, {1.4f, 1.0f, 1.2f}},
        {{  6.0f, -kPatchCameraHeight + 0.5f, -16.0f}, {2.0f, 1.4f, 1.7f}},
        {{ -9.0f, -kPatchCameraHeight + 0.6f, -24.0f}, {2.6f, 1.8f, 2.2f}},
        {{ 12.0f, -kPatchCameraHeight + 0.4f, -34.0f}, {1.8f, 1.2f, 1.6f}},
        {{  2.0f, -kPatchCameraHeight + 0.9f, -52.0f}, {4.2f, 2.6f, 3.6f}},
        {{-20.0f, -kPatchCameraHeight + 1.1f, -70.0f}, {5.5f, 3.2f, 4.6f}},
    }};
    rocks.assign(boulders.begin(), boulders.end());
    for (int i = 0; i < 26; ++i) {
        const float angle = next() * 6.2831853f;
        const float range = 8.0f + next() * 240.0f;
        const float size = 0.25f + next() * next() * 3.0f;
        rocks.push_back({{range * std::sin(angle), -static_cast<float>(kPatchCameraHeight) + size * 0.35f,
                          -range * std::cos(angle)},
                         {size * (0.8f + next() * 0.6f), size * (0.5f + next() * 0.4f), size * (0.8f + next() * 0.6f)}});
    }
    return rocks;
}

inline std::vector<NamedRecipe> BuildRecipes() {
    std::vector<NamedRecipe> recipes;

    // Unit-radius sphere: sun, planet, moon, probe and lamps. The instance scale carries the
    // radius so each body keeps its SDF evaluation in unit-ish local coordinates.
    recipes.push_back({RecipeSlot::UnitSphere, "unit-sphere",
        UnionOf({SphereOp({0.0f, 0.0f, 0.0f}, 1.0f)}, glm::vec3(0.0f), 1.001f)});

    // Station: long axis +Y (cross-track), 100 m tip to tip. Local +X is the velocity, +Z is up.
    const float tip = static_cast<float>(kStationHalfLength);
    recipes.push_back({RecipeSlot::StationHull, "station-hull",
        UnionOf({CapsuleOp(tip - 6.0f, 6.0f),
                 EllipsoidOp({0.0f, 18.0f, 0.0f}, {14.0f, 5.0f, 14.0f}),
                 EllipsoidOp({0.0f, -18.0f, 0.0f}, {14.0f, 5.0f, 14.0f})},
                glm::vec3(0.0f), 64.0f)});
    recipes.push_back({RecipeSlot::StationAccent, "station-accent",
        UnionOf({EllipsoidOp({0.0f, 0.0f, 0.0f}, {8.0f, 10.0f, 8.0f}),
                 EllipsoidOp({0.0f, 38.0f, 0.0f}, {7.5f, 2.5f, 7.5f}),
                 EllipsoidOp({0.0f, -38.0f, 0.0f}, {7.5f, 2.5f, 7.5f})},
                glm::vec3(0.0f), 64.0f)});
    recipes.push_back({RecipeSlot::StationPanels, "station-panels",
        UnionOf({BoxOp({0.0f, 30.0f, 0.0f}, {0.2f, 10.0f, 30.0f}, 0.1f),
                 BoxOp({0.0f, -30.0f, 0.0f}, {0.2f, 10.0f, 30.0f}, 0.1f)},
                glm::vec3(0.0f), 64.0f)});

    // Near-surface patch in metres, patch-local frame: +Y up, camera 1.7 m above the ground
    // at the origin. The slab top is at y = -1.7; rocks sit on it.
    recipes.push_back({RecipeSlot::TerrainGround, "terrain-ground",
        UnionOf({BoxOp({0.0f, -static_cast<float>(kPatchCameraHeight) - 1.0f, 0.0f}, {2000.0f, 1.0f, 2000.0f}, 0.0f)},
                {0.0f, -static_cast<float>(kPatchCameraHeight) - 1.0f, 0.0f}, 2900.0f)});
    std::vector<Vixen::SVO::Recipe::SdfInstruction> rockOps;
    for (const RockPlacement& rock : PatchRocks()) rockOps.push_back(EllipsoidOp(rock.center, rock.radii));
    recipes.push_back({RecipeSlot::TerrainRocks, "terrain-rocks", UnionOf(rockOps, glm::vec3(0.0f), 330.0f)});
    return recipes;
}

inline float SrgbChannelToLinear(float s) {
    return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

inline glm::vec3 Srgb8ToLinear(uint32_t rgb) {
    auto decode = [](uint32_t value) { return SrgbChannelToLinear(static_cast<float>(value) / 255.0f); };
    return {decode((rgb >> 16) & 0xffu), decode((rgb >> 8) & 0xffu), decode(rgb & 0xffu)};
}

// Circular Kepler motion in double: radius * (cos(wt + phase) a + sin(wt + phase) b).
inline glm::dvec3 CircularPosition(const glm::dvec3& a, const glm::dvec3& b, double radius,
                                   double period, double phase, double t) {
    const double angle = 2.0 * kPi * t / period + phase;
    return radius * (std::cos(angle) * a + std::sin(angle) * b);
}

inline glm::dvec3 CircularVelocityDirection(const glm::dvec3& a, const glm::dvec3& b, double period,
                                            double phase, double t) {
    const double angle = 2.0 * kPi * t / period + phase;
    return -std::sin(angle) * a + std::cos(angle) * b;
}

inline const glm::dvec3 kAxisX{1.0, 0.0, 0.0};
inline const glm::dvec3 kAxisY{0.0, 1.0, 0.0};
inline const glm::dvec3 kAxisZ{0.0, 0.0, 1.0};

struct BodyPlacement {
    const char* name;
    RecipeSlot recipe;
    glm::dvec3 position;      // sim frame, metres
    glm::dmat3 rotation;      // columns are the body axes in the sim frame
    double scale;             // uniform local-to-world scale (the radius for unit spheres)
    glm::vec3 color;
    float emission;
};

struct ResolvedState {
    std::string name;
    uint32_t width = kCaptureExtent;
    uint32_t height = kCaptureExtent;
    float verticalFovDegrees = 45.0f;
    float exposureCompensationEV = 0.0f;  // set by ResolveState from the meter's expected reading
    glm::dvec3 camera{0.0};
    glm::dvec3 forward{0.0, 0.0, -1.0};
    std::string focus;                  // body the directional light's lit mask is evaluated at
    glm::dvec3 sunDirection{1.0, 0.0, 0.0};   // unit, from the focus body toward the sun
    float shadowBias = 0.01f;           // world metres, one value per frame (ShadowConfigNode)
    float shadowMaxDistance = 1000.0f;  // stops before the sun instance so it never shadows
    double sunDistance = kAstronomicalUnit;
    std::vector<BodyPlacement> bodies;
    // Station frame (only meaningful when the station is present).
    glm::dvec3 stationVelocity{0.0}, stationNormal{0.0}, stationUp{0.0};
    double stationPhaseDegrees = 0.0;
    double moonPhaseDegrees = 0.0;
    double timeSeconds = 0.0;
};

inline glm::dmat3 FrameColumns(const glm::dvec3& x, const glm::dvec3& y, const glm::dvec3& z) {
    return glm::dmat3(x, y, z);
}

inline glm::dvec3 Unit(const glm::dvec3& v) { return glm::normalize(v); }

inline glm::dvec3 Rotate(const glm::dvec3& v, const glm::dvec3& axis, double radians) {
    const glm::dvec3 k = Unit(axis);
    return v * std::cos(radians) + glm::cross(k, v) * std::sin(radians) +
           k * glm::dot(k, v) * (1.0 - std::cos(radians));
}

inline std::vector<std::string> StateNames() {
    return {"C1", "C1-epic", "C2", "C2-term", "C4a", "C4b", "C4c",
            "C5a-moon", "C7", "C7-60", "C7-60-1p5au", "C9"};
}

// States named in planetref section 3.3 versus witnesses added by the planetfix lane.
inline bool IsSpecState(const std::string& name) {
    return name == "C1" || name == "C2" || name == "C4a" || name == "C4b" ||
           name == "C4c" || name == "C7" || name == "C9";
}

inline void AddStation(ResolvedState& s, const glm::dvec3& planet, double stationPhase, double t,
                       size_t lampCount) {
    const glm::dvec3 b = std::cos(kStationInclination) * kAxisZ + std::sin(kStationInclination) * kAxisY;
    const glm::dvec3 offset = CircularPosition(kAxisX, b, kStationOrbitRadius, kStationPeriod, stationPhase, t);
    const glm::dvec3 velocity = CircularVelocityDirection(kAxisX, b, kStationPeriod, stationPhase, t);
    const glm::dvec3 up = Unit(offset);
    const glm::dvec3 normal = Unit(glm::cross(up, velocity));
    s.stationVelocity = velocity;
    s.stationNormal = normal;
    s.stationUp = up;
    const glm::dvec3 position = planet + offset;
    const glm::dmat3 rotation = FrameColumns(velocity, normal, up);
    s.bodies.push_back({"station-hull", RecipeSlot::StationHull, position, rotation, 1.0,
                        Srgb8ToLinear(0xe8e1da), 0.0f});
    s.bodies.push_back({"station-accent", RecipeSlot::StationAccent, position, rotation, 1.0,
                        Srgb8ToLinear(0xfa9f4e), 0.0f});
    s.bodies.push_back({"station-panels", RecipeSlot::StationPanels, position, rotation, 1.0,
                        Srgb8ToLinear(0x3c4045), 0.0f});
    const glm::vec3 lampColor = Srgb8ToLinear(0xf9b55c);
    auto lamp = [&](const char* name, const glm::dvec3& local) {
        s.bodies.push_back({name, RecipeSlot::UnitSphere, position + rotation * local,
                            glm::dmat3(1.0), 0.45, lampColor, kStationLampEmission});
    };
    if (lampCount == 2) {
        lamp("station-lamp-0", {-6.5, 22.0, 0.0});
        lamp("station-lamp-1", {-6.5, -22.0, 0.0});
    } else {
        // Lamp lattice on the spine: ten rings of lampCount/10, staggered by ring.
        const size_t perRing = lampCount / 10;
        for (size_t ring = 0; ring < 10; ++ring) {
            const double y = -40.0 + 80.0 * static_cast<double>(ring) / 9.0;
            for (size_t i = 0; i < perRing; ++i) {
                const double angle = 2.0 * kPi * (static_cast<double>(i) + 0.5 * static_cast<double>(ring & 1u)) /
                                     static_cast<double>(perRing);
                lamp("station-lamp", {6.45 * std::cos(angle), y, 6.45 * std::sin(angle)});
            }
        }
    }
    s.bodies.push_back({"probe", RecipeSlot::UnitSphere, position + kProbeLead * velocity,
                        glm::dmat3(1.0), kProbeRadius, glm::vec3(kProbeAlbedo), 0.0f});
}

// The shadow bias is one value per frame (ShadowConfigNode) and, at planet scale, the shadow
// ray's own hit tolerance is far above the float ulp; 1.25 pixel footprints at the focus range
// is the smallest value measured to remove self-shadow rings on the planet disc.
inline float FootprintBias(double range, float verticalFovDegrees, uint32_t height) {
    const double footprint = range * 2.0 * std::tan(0.5 * verticalFovDegrees * kDegrees) / height;
    return static_cast<float>(std::max(1.25 * footprint, 0.01));
}

inline std::optional<ResolvedState> ResolveState(const std::string& name) {
    ResolvedState s;
    s.name = name;

    double auFactor = 1.0;
    double timeSeconds = 0.0;
    double moonPhase = 2.0 * kDegrees;      // 2 degrees off the sun-planet line
    double stationPhase = 0.0;
    bool stationPresent = false;
    size_t lampCount = 2;
    bool patch = false;

    if (name == "C7-60-1p5au") auFactor = 1.5;

    // Station phases: the shadow edge of the cylinder umbra is at cos(psi) = -cos(asin(R/r)).
    const double edgePsi = std::acos(-std::cos(std::asin(kPlanetRadius / kStationOrbitRadius)));

    if (name == "C2" || name == "C2-term") {
        stationPresent = true;
        // Sun elevation -4 deg at nadir, on the dawn side: the station moves toward the day, so
        // "forward" looks toward the terminator and the lit hemisphere.
        stationPhase = 2.0 * kPi - std::acos(std::sin(-4.0 * kDegrees));
    } else if (name == "C4a" || name == "C4b" || name == "C4c") {
        stationPresent = true;
        stationPhase = edgePsi;
        timeSeconds = name == "C4a" ? -10.0 : (name == "C4b" ? 0.0 : 10.0);
    } else if (name == "C9") {
        stationPresent = true;
        stationPhase = 180.0 * kDegrees;
        moonPhase = 175.0 * kDegrees;       // full moon, off the shadow axis
        lampCount = 150;
    } else if (name == "C5a-moon") {
        moonPhase = 90.0 * kDegrees;
    } else if (name == "C7" || name == "C7-60" || name == "C7-60-1p5au") {
        patch = true;
    } else if (name != "C1" && name != "C1-epic") {
        return std::nullopt;
    }

    // Corner-tile meter readings measured on Dozen: sky in most states, the unlit planet (0) in C2,
    // and a steeper sky gradient across the 128 px C9 frame.
    const double cornerEv100 = (name == "C2" || name == "C2-term") ? 0.0
                             : (name == "C9" ? 5.486432 : kSkyCornerEv100);
    s.exposureCompensationEV = ExposureFor(cornerEv100);
    s.timeSeconds = timeSeconds;
    s.stationPhaseDegrees = stationPhase / kDegrees;
    s.moonPhaseDegrees = moonPhase / kDegrees;

    // Planet around the sun in the XZ plane; the epoch puts it at -X so the sun is +X from it.
    const glm::dvec3 planetPos = CircularPosition(kAxisX, kAxisZ, auFactor * kAstronomicalUnit,
                                                  kPlanetPeriod, kPi, timeSeconds);
    const glm::dvec3 moonB = std::cos(kMoonInclination) * kAxisZ + std::sin(kMoonInclination) * kAxisY;
    const glm::dvec3 moonPos = planetPos + CircularPosition(kAxisX, moonB, kMoonOrbitRadius,
                                                            kMoonPeriod, moonPhase, timeSeconds);

    if (patch) {
        // Patch-local render frame: +Y up, +X east, -Z north; sun 14 degrees above the east
        // horizon. The planet sphere is omitted: the uber path's EPS_MIN (5e-6 local units)
        // is 32 m at planet scale, larger than the camera height.
        const glm::dvec3 sunDir{std::cos(kPatchSunElevation), std::sin(kPatchSunElevation), 0.0};
        s.camera = glm::dvec3(0.0);
        s.focus = "patch";
        s.sunDirection = sunDir;
        s.sunDistance = auFactor * kAstronomicalUnit;
        s.verticalFovDegrees = name == "C7" ? 50.0f : 60.0f;
        if (name == "C7") {
            const double pitch = -3.0 * kDegrees;
            s.forward = Unit({0.0, std::sin(pitch), -std::cos(pitch)});
        } else {
            const double pitch = 10.0 * kDegrees;
            const double azimuth = 20.0 * kDegrees;    // from east toward north
            s.forward = Unit({std::cos(pitch) * std::cos(azimuth), std::sin(pitch),
                              -std::cos(pitch) * std::sin(azimuth)});
        }
        s.shadowBias = 0.01f;
        s.shadowMaxDistance = 1000.0f;
        s.bodies.push_back({"sun", RecipeSlot::UnitSphere, sunDir * s.sunDistance, glm::dmat3(1.0), kSunRadius,
                            glm::vec3(1.0f, 0.93f, 0.82f), kSunEmission});
        s.bodies.push_back({"terrain-ground", RecipeSlot::TerrainGround, glm::dvec3(0.0), glm::dmat3(1.0), 1.0,
                            Srgb8ToLinear(0xb9a88f), 0.0f});
        s.bodies.push_back({"terrain-rocks", RecipeSlot::TerrainRocks, glm::dvec3(0.0), glm::dmat3(1.0), 1.0,
                            Srgb8ToLinear(0xeac794), 0.0f});
        return s;
    }

    s.bodies.push_back({"sun", RecipeSlot::UnitSphere, glm::dvec3(0.0), glm::dmat3(1.0), kSunRadius,
                        glm::vec3(1.0f, 0.93f, 0.82f), kSunEmission});
    s.bodies.push_back({"planet", RecipeSlot::UnitSphere, planetPos, glm::dmat3(1.0), kPlanetRadius,
                        glm::vec3(kPlanetAlbedo), 0.0f});
    s.bodies.push_back({"moon", RecipeSlot::UnitSphere, moonPos, glm::dmat3(1.0), kMoonRadius,
                        glm::vec3(kMoonAlbedo), 0.0f});
    if (stationPresent) AddStation(s, planetPos, stationPhase, timeSeconds, lampCount);

    auto setFocus = [&](const char* focus, const glm::dvec3& position) {
        s.focus = focus;
        s.sunDirection = Unit(-position);
        s.sunDistance = glm::length(position);
        s.shadowMaxDistance = static_cast<float>(0.99 * (s.sunDistance - kSunRadius));
    };

    if (name == "C2" || name == "C2-term" || name == "C4a" || name == "C4b" || name == "C4c") {
        s.width = s.height = kHeavyCaptureExtent;
    } else if (name == "C9") {
        s.width = s.height = kLampLatticeExtent;
    }

    if (name == "C1" || name == "C1-epic") {
        const double offset = name == "C1" ? 0.0 : 7.63 * kDegrees;   // sun-earth-vehicle angle
        const glm::dvec3 direction{std::cos(offset), 0.0, std::sin(offset)};
        s.camera = planetPos + 1.586e9 * direction;
        s.forward = Unit(planetPos - s.camera);
        s.verticalFovDegrees = 0.6f;
        s.shadowBias = FootprintBias(glm::length(planetPos - s.camera) - kPlanetRadius, s.verticalFovDegrees, s.height);
        setFocus("planet", planetPos);
    } else if (name == "C5a-moon") {
        const glm::dvec3 toMoon = Unit(moonPos - planetPos);
        s.camera = planetPos + (kPlanetRadius + 5.0e7) * toMoon;
        s.forward = Unit(moonPos - s.camera);
        s.verticalFovDegrees = 5.0f;
        s.shadowBias = FootprintBias(glm::length(moonPos - s.camera) - kMoonRadius, s.verticalFovDegrees, s.height);
        setFocus("moon", moonPos);
    } else if (name == "C2" || name == "C2-term") {
        const glm::dvec3 stationPos = s.bodies[3].position;
        s.camera = stationPos;
        const double tilt = name == "C2" ? 30.0 * kDegrees : 47.0 * kDegrees;    // from nadir toward the velocity; 47 deg centres the terminator
        s.forward = Unit(std::cos(tilt) * -s.stationUp + std::sin(tilt) * s.stationVelocity);
        s.verticalFovDegrees = 17.0f;
        s.shadowBias = FootprintBias(kStationAltitude / std::cos(tilt), s.verticalFovDegrees, s.height);
        setFocus("planet", planetPos);
        // The camera sits at the station's centre, inside the hull: drop the station's own geometry.
        s.bodies.erase(std::remove_if(s.bodies.begin(), s.bodies.end(),
                                      [](const BodyPlacement& b) {
                                          return std::string(b.name).rfind("station-", 0) == 0;
                                      }),
                       s.bodies.end());
    } else {
        // C4a/b/c and C9: chase camera 300 m behind the station on its track, looking forward.
        const glm::dvec3 stationPos = s.bodies[3].position;
        s.camera = stationPos - 300.0 * s.stationVelocity;
        s.forward = Unit(stationPos - s.camera);
        s.verticalFovDegrees = 45.0f;
        s.shadowBias = 0.01f;
        setFocus("station", stationPos);
    }
    return s;
}

inline Vixen::SVO::Affine3x4Gpu ToAffineD(const glm::dmat4& matrix) {
    Vixen::SVO::Affine3x4Gpu result{};
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 4; ++column)
            result.rows[row][column] = static_cast<float>(matrix[column][row]);
    return result;
}

// Camera-relative upload (D2): both matrices are built and inverted in double, the camera
// subtraction happens in double, and only the small result is cast to float.
inline std::vector<Vixen::SVO::BodyInstanceGpu> BodyInstances(const ResolvedState& state) {
    std::vector<Vixen::SVO::BodyInstanceGpu> instances;
    instances.reserve(state.bodies.size());
    for (const BodyPlacement& body : state.bodies) {
        glm::dmat4 localToWorld(1.0);
        localToWorld[0] = glm::dvec4(body.rotation[0] * body.scale, 0.0);
        localToWorld[1] = glm::dvec4(body.rotation[1] * body.scale, 0.0);
        localToWorld[2] = glm::dvec4(body.rotation[2] * body.scale, 0.0);
        localToWorld[3] = glm::dvec4(body.position - state.camera, 1.0);
        Vixen::SVO::BodyInstanceGpu instance{};
        instance.transform.localToWorld = ToAffineD(localToWorld);
        instance.transform.worldToLocal = ToAffineD(glm::inverse(localToWorld));
        Vixen::SVO::BodyInstanceMaterialGpu& material = instance.material;
        material.color[0] = body.color.r;
        material.color[1] = body.color.g;
        material.color[2] = body.color.b;
        material.providerKind = 1u;
        material.recipeId = static_cast<uint32_t>(body.recipe);
        material.recipeParams[3] = body.emission;
        instances.push_back(instance);
    }
    return instances;
}

// Today's single directional light stands in for the sun (D1); the sun recipe is drawn but inert.
inline std::vector<Vixen::Gpu::Light> Lights(const ResolvedState& state) {
    Vixen::Gpu::Light sun{};
    sun.direction_or_positionX = static_cast<float>(state.sunDirection.x);
    sun.direction_or_positionY = static_cast<float>(state.sunDirection.y);
    sun.direction_or_positionZ = static_cast<float>(state.sunDirection.z);
    sun.kind = 0u;
    sun.radianceX = kSunLightRadiance;
    sun.radianceY = kSunLightRadiance;
    sun.radianceZ = kSunLightRadiance;
    return {sun};
}

inline std::vector<float> CelSpillPurposeScales() { return {1.0f}; }

inline constexpr float kAmbient = 0.0f;

} // namespace Vixen::App::Planet
