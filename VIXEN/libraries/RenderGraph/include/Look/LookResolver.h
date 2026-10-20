// Copyright (C) 2025 Lior Yanai (eLiorg). Licensed under the MIT License.
#pragma once

// The CPU look resolver (R673). Every look value reaching LightingConfigNode comes from here: ordered layers
// (scene < volumes < flow < dev overrides, priority within a layer), each a partial look whose channels are
// unset or a value, blended per channel in that channel's own space.

#include "Generated/LightingConfig.g.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace Vixen::RenderGraph {

enum class LookLayer : uint8_t { Scene = 0, Volume = 1, Flow = 2, DevOverride = 3 };

// The light set is one discrete channel: ambient and per-light purpose scales travel with the lights.
struct LookLightSet {
    std::array<Vixen::Gpu::Light, 4> lights{};
    uint32_t lightCount = 0;
    float ambientIntensity = 0.3f;
    std::array<float, 4> celSpillPurposeScales{1.0f, 1.0f, 1.0f, 1.0f};

    // Excess lights are clipped to the capacity of four; missing purpose scales default to 1.0.
    static LookLightSet From(const std::vector<Vixen::Gpu::Light>& lights, float ambientIntensity,
                             const std::vector<float>& celSpillPurposeScales = {}) {
        LookLightSet set;
        set.ambientIntensity = ambientIntensity;
        set.lightCount = static_cast<uint32_t>(std::min(lights.size(), set.lights.size()));
        for (uint32_t i = 0; i < set.lightCount; ++i) {
            set.lights[i] = lights[i];
            set.celSpillPurposeScales[i] = i < celSpillPurposeScales.size() ? celSpillPurposeScales[i] : 1.0f;
        }
        return set;
    }
};

// The fully resolved look. Defaults are the node's own, so an empty resolver reproduces the pre-resolver upload.
struct ResolvedLook {
    // Exposure window (EV100 values and the key blend in log2 / EV space; the rest linearly).
    float exposureKeyValue = 0.18f;
    float exposureMinEV100 = -8.0f;
    float exposureMaxEV100 = 12.0f;
    float exposureLowPercent = 50.0f;
    float exposureHighPercent = 95.0f;
    float exposureSpeedUp = 3.0f;
    float exposureSpeedDown = 1.0f;
    float exposureCompensationEV = 0.0f;
    float saturation = 1.0f;
    float celShadowThreshold = 0.16f;
    float celLitThreshold = 0.86f;
    float celRampSoftness = 0.08f;
    float dimBandFloor = 0.0f;
    uint32_t skyMode = 0;
    std::array<float, 3> skyColor{0.02f, 0.02f, 0.06f};
    float skyGradientSlope = 0.4f;
    std::optional<LookLightSet> lights;   // unset: the node's default light
};

// One layer's partial look: a channel is unset or carries a value.
struct PartialLook {
    std::optional<float> exposureKeyValue, exposureMinEV100, exposureMaxEV100, exposureLowPercent,
        exposureHighPercent, exposureSpeedUp, exposureSpeedDown, exposureCompensationEV, saturation,
        celShadowThreshold, celLitThreshold, celRampSoftness, dimBandFloor, skyGradientSlope;
    std::optional<uint32_t> skyMode;
    std::optional<std::array<float, 3>> skyColor;
    std::optional<LookLightSet> lights;
};

struct LookContribution {
    LookLayer layer = LookLayer::Scene;
    int priority = 0;       // higher wins within a layer
    float weight = 1.0f;    // [0, 1]; 1 overrides, 0 leaves the lower result untouched
    // Relative mode adds weight * value to the lower result (in the channel's blend space) instead of
    // overriding it. Continuous channels only; off unless a concrete case asks for it.
    bool relative = false;
    PartialLook look;
};

class LookResolver {
public:
    using Handle = uint32_t;

    // A Handle stays valid until Remove; Set replaces the contribution it names.
    Handle Add(LookContribution contribution) {
        const Handle handle = nextHandle_++;
        entries_.push_back({handle, std::move(contribution)});
        return handle;
    }
    bool Set(Handle handle, LookContribution contribution) {
        for (Entry& entry : entries_) {
            if (entry.handle == handle) { entry.contribution = std::move(contribution); return true; }
        }
        return false;
    }
    bool Remove(Handle handle) {
        const auto it = std::find_if(entries_.begin(), entries_.end(),
                                     [handle](const Entry& e) { return e.handle == handle; });
        if (it == entries_.end()) return false;
        entries_.erase(it);
        return true;
    }
    // Replaces the single contribution a layer's owner keeps under a stable slot (scene look, dev overrides).
    void SetSlot(LookLayer layer, uint32_t slot, LookContribution contribution) {
        const Handle handle = SlotHandle(layer, slot);
        contribution.layer = layer;
        if (!Set(handle, contribution)) {
            entries_.push_back({handle, std::move(contribution)});
        }
    }
    size_t Size() const { return entries_.size(); }

    ResolvedLook Resolve() const {
        std::vector<const Entry*> ordered;
        ordered.reserve(entries_.size());
        for (const Entry& entry : entries_) ordered.push_back(&entry);
        std::stable_sort(ordered.begin(), ordered.end(), [](const Entry* a, const Entry* b) {
            if (a->contribution.layer != b->contribution.layer) return a->contribution.layer < b->contribution.layer;
            return a->contribution.priority < b->contribution.priority;
        });

        ResolvedLook out;
        for (const Entry* entry : ordered) Apply(out, entry->contribution);

        // The meter snaps to its target at speed 0: a Fixed window (zero width) is the only case that asks for it.
        if (out.exposureMaxEV100 <= out.exposureMinEV100) {
            out.exposureMaxEV100 = out.exposureMinEV100;
            out.exposureSpeedUp = 0.0f;
            out.exposureSpeedDown = 0.0f;
        }
        return out;
    }

private:
    struct Entry {
        Handle handle;
        LookContribution contribution;
    };

    static Handle SlotHandle(LookLayer layer, uint32_t slot) {
        return 0x80000000u | (static_cast<uint32_t>(layer) << 24) | (slot & 0x00ffffffu);
    }

    static float Clamp01(float w) { return std::isfinite(w) ? std::clamp(w, 0.0f, 1.0f) : 0.0f; }

    // Linear-space channel: lerp(lower, value, w) (relative: lower + w * value).
    static void Linear(float& dst, const std::optional<float>& v, float w, bool relative) {
        if (!v) return;
        dst = relative ? dst + w * *v : (w >= 1.0f ? *v : dst + (*v - dst) * w);
    }
    // EV/log2 channel: a linear blend of EV values is a log2 blend of exposure multipliers.
    static void Ev(float& dst, const std::optional<float>& v, float w, bool relative) { Linear(dst, v, w, relative); }
    // Log-space channel for a positive linear quantity (the key value): blend log2, return to linear.
    static void LogKey(float& dst, const std::optional<float>& v, float w, bool relative) {
        if (!v) return;
        if (!relative && w >= 1.0f) { dst = *v; return; }
        const float lower = std::log2(std::max(dst, 1.0e-6f));
        const float value = std::log2(std::max(*v, 1.0e-6f));
        dst = std::exp2(relative ? lower + w * value : lower + (value - lower) * w);
    }
    // Discrete channels never interpolate: the contribution takes over once it carries at least half weight.
    template <typename T>
    static void Discrete(T& dst, const std::optional<T>& v, float w) {
        if (v && w >= 0.5f) dst = *v;
    }

    static void Apply(ResolvedLook& out, const LookContribution& c) {
        const float w = Clamp01(c.weight);
        if (w <= 0.0f) return;
        const PartialLook& p = c.look;
        const bool rel = c.relative;
        LogKey(out.exposureKeyValue, p.exposureKeyValue, w, rel);
        Ev(out.exposureMinEV100, p.exposureMinEV100, w, rel);
        Ev(out.exposureMaxEV100, p.exposureMaxEV100, w, rel);
        Linear(out.exposureLowPercent, p.exposureLowPercent, w, rel);
        Linear(out.exposureHighPercent, p.exposureHighPercent, w, rel);
        Linear(out.exposureSpeedUp, p.exposureSpeedUp, w, rel);
        Linear(out.exposureSpeedDown, p.exposureSpeedDown, w, rel);
        Ev(out.exposureCompensationEV, p.exposureCompensationEV, w, rel);
        Linear(out.saturation, p.saturation, w, rel);
        Linear(out.celShadowThreshold, p.celShadowThreshold, w, rel);
        Linear(out.celLitThreshold, p.celLitThreshold, w, rel);
        Linear(out.celRampSoftness, p.celRampSoftness, w, rel);
        Linear(out.dimBandFloor, p.dimBandFloor, w, rel);
        Linear(out.skyGradientSlope, p.skyGradientSlope, w, rel);
        if (p.skyColor) {
            for (size_t i = 0; i < 3; ++i) {
                const std::optional<float> channel = (*p.skyColor)[i];
                Linear(out.skyColor[i], channel, w, rel);
            }
        }
        Discrete(out.skyMode, p.skyMode, w);
        if (p.lights && w >= 0.5f) out.lights = p.lights;
    }

    std::vector<Entry> entries_;
    Handle nextHandle_ = 1;
};

}  // namespace Vixen::RenderGraph
