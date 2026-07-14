// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once
// ============================================================================
// CornellBoxSceneDefinition.h — Sampled Lighting Cornell Box GI demo, M1.
//
// ONE shared source of geometry/color/camera/probe-grid numbers for BOTH demo
// variants (VIXEN_DDGI_CORNELL_BAKED_DEMO in M1, VIXEN_DDGI_CORNELL_VIRTUAL_DEMO
// in M2) — per the plan's own "ideally visually identical" constraint, enforced
// by construction: both variants bake/register bodies FROM these same numbers,
// not from two independently-authored scenes. See
// Vixen-Docs/01-Architecture/Sampled-Lighting-Cornell-Box-Demo-Plan-2026-07.md.
//
// World-space placement is constrained by ProbeGridConfigNode's own build-time
// default grid (MakeDefaultProbeGridConfig, ProbeGridConfigNode.cpp): origin
// (0,0,0), spacing 4, count 8x8x8 -> covers world [0,32) on every axis, with NO
// live setter this milestone (same constraint VIXEN_DDGI_LEAK_GATE_DEMO's own
// scene had to satisfy, BuildRenderGraph.cpp's leak-gate block header comment).
// The box interior below is sized/centered to sit entirely inside that cube.
// ============================================================================
#include <glm/glm.hpp>
#include <cstdint>

namespace Vixen::App::CornellBox {

// --- Box interior -----------------------------------------------------------
// Interior half-extents (world units) and center — a 20x20x20 cube centered at
// grid-center (16,16,16), well inside the fixed [0,32) probe-grid coverage on
// every axis (6-unit margin to the grid boundary on all sides for probes near
// the walls to still have valid neighbor samples).
constexpr glm::vec3 kBoxCenter(16.0f, 16.0f, 16.0f);
constexpr float      kBoxHalfExtent = 10.0f;      // interior half-size
constexpr float      kWallThickness = 1.0f;       // wall slab half-thickness (world units)

// --- Wall material tints (per-instance color, BodyInstanceGpu.color[3]) -----
constexpr glm::vec3 kLeftWallColor (0.75f, 0.15f, 0.15f);  // red tint  (-X)
constexpr glm::vec3 kRightWallColor(0.15f, 0.65f, 0.15f);  // green tint (+X)
constexpr glm::vec3 kNeutralWallColor(0.85f, 0.85f, 0.82f); // back/floor/ceiling — white/neutral

// --- Ceiling emissive area light --------------------------------------------
// A flattened box recessed just below the ceiling, centered on X/Z, emissive.
constexpr glm::vec3 kLightCenter(kBoxCenter.x, kBoxCenter.y + kBoxHalfExtent - 1.5f, kBoxCenter.z);
constexpr glm::vec3 kLightHalfExtent(4.0f, 0.3f, 4.0f);
constexpr float      kLightEmissionIntensity = 6.0f;
constexpr glm::vec3  kLightColor(1.0f, 0.97f, 0.9f);

// --- Diffuse interior objects (non-emissive) --------------------------------
// Two objects placed off-center so bounce-light color bleed from the tinted
// walls is visible on each (near the red wall / near the green wall).
constexpr glm::vec3 kSphereObjectCenter(kBoxCenter.x - 5.0f, kBoxCenter.y - 6.5f, kBoxCenter.z - 2.0f);
constexpr float      kSphereObjectRadius = 3.0f;
constexpr glm::vec3  kSphereObjectColor(0.9f, 0.9f, 0.9f);

constexpr glm::vec3 kBoxObjectCenter(kBoxCenter.x + 5.0f, kBoxCenter.y - 7.0f, kBoxCenter.z + 3.0f);
constexpr glm::vec3 kBoxObjectHalfExtent(2.2f, 3.0f, 2.2f);
constexpr glm::vec3  kBoxObjectColor(0.9f, 0.9f, 0.9f);

// --- Camera: framed straight-on into the box's open face (-Z face open) ----
// Orbit-mode preset (mirrors every Tiered-ESVO demo's own orbitCenter convention
// for a body NOT at the stale (5,5,5) legacy default): orbitCenter = box center,
// camera pulled back along -Z looking toward +Z into the open face.
constexpr glm::vec3 kCameraOrbitCenter = kBoxCenter;
constexpr float      kCameraOrbitDistance = 34.0f;
constexpr float      kCameraFovDegrees = 60.0f;

// --- DDGI probe grid ---------------------------------------------------------
// MUST match ProbeGridConfigNode's own build-time MakeDefaultProbeGridConfig
// default exactly (no live setter exists this milestone) — documented here so
// both variants' authors can see the geometry constraint the grid imposes,
// even though neither variant sets these values directly.
constexpr float      kProbeGridOrigin = 0.0f;
constexpr float      kProbeGridSpacing = 4.0f;
constexpr uint32_t    kProbeGridCount = 8u;  // 8x8x8, world coverage [0,32) per axis

}  // namespace Vixen::App::CornellBox
