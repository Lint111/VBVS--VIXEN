#include "Data/Nodes/LightingConfigNodeConfig.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

// CPU mirror of ExposureMeter.comp (luminance histogram) and ExposureReduce.comp
// (percentile-window metering, EV100 clamp, frame-rate independent adaptation).
// The constants and arithmetic follow ExposureHistogram.glsl line for line.
// Fixtures contain single-channel radiance; no GPU readback participates. The preset
// and fixed-mode tests read the engine's own LightingConfigNodeConfig table.
namespace {

constexpr uint32_t kBins = 128u;
constexpr float kLog2Min = -20.0f;
constexpr float kBinsPerEV = 4.0f;
constexpr float kLog2HundredOverK = 3.0f;

struct ExposureSettings {
    float keyValue = 0.18f;
    float minEV100 = -8.0f;
    float maxEV100 = 12.0f;
    float lowPercent = 50.0f;
    float highPercent = 95.0f;
    float speedUp = 3.0f;
    float speedDown = 1.0f;
};

struct MeterState {
    float log2Exposure = 0.0f;
    bool adapted = false;
    float meteredEV100 = 0.0f;
    float targetLog2Exposure = 0.0f;
};

using Histogram = std::array<uint32_t, kBins>;

uint32_t MirrorBin(float luminance) {
    if (luminance <= 1.0e-6f) return 0u;
    const float scaled = (std::log2(luminance) - kLog2Min) * kBinsPerEV;
    return static_cast<uint32_t>(std::clamp(scaled, 0.0f, static_cast<float>(kBins - 1u)));
}

Histogram MirrorHistogram(const std::vector<float>& luminance) {
    Histogram histogram{};
    for (float sample : luminance) {
        if (std::isnan(sample) || std::isinf(sample)) continue;
        ++histogram[MirrorBin(std::max(sample, 0.0f))];
    }
    return histogram;
}

float BinCenterLog2(uint32_t bin) {
    return kLog2Min + (static_cast<float>(bin) + 0.5f) / kBinsPerEV;
}

// One ExposureReduce dispatch: meter the histogram, then adapt by deltaSeconds.
MeterState MirrorReduce(const Histogram& histogram, const ExposureSettings& settings,
                        MeterState previous, float deltaSeconds) {
    uint32_t total = 0u;
    for (uint32_t count : histogram) total += count;
    MeterState next = previous;
    float target = previous.adapted ? previous.targetLog2Exposure : 0.0f;
    if (total != 0u) {
        const float lowPercent = std::clamp(settings.lowPercent, 0.0f, 100.0f);
        const float highPercent = std::clamp(settings.highPercent, lowPercent, 100.0f);
        const float lowCount = static_cast<float>(total) * lowPercent * 0.01f;
        const float highCount = static_cast<float>(total) * highPercent * 0.01f;
        float below = 0.0f, weightedLog = 0.0f, weight = 0.0f, lastBinLog = kLog2Min;
        for (uint32_t b = 0; b < kBins; ++b) {
            const float count = static_cast<float>(histogram[b]);
            const float binLog = BinCenterLog2(b);
            const float taken = std::clamp(std::min(below + count, highCount) - std::max(below, lowCount),
                                           0.0f, count);
            weightedLog += taken * binLog;
            weight += taken;
            if (count > 0.0f && below < highCount) lastBinLog = binLog;
            below += count;
        }
        const float metered = (weight > 0.0f ? weightedLog / weight : lastBinLog) + kLog2HundredOverK;
        const float clamped = std::clamp(metered, settings.minEV100, std::max(settings.maxEV100, settings.minEV100));
        target = std::log2(std::max(settings.keyValue, 1.0e-4f)) - (clamped - kLog2HundredOverK);
        next.meteredEV100 = metered;
    }
    float value = target;
    if (previous.adapted) {
        const float rate = target < previous.log2Exposure ? settings.speedUp : settings.speedDown;
        const float step = std::max(deltaSeconds, 0.0f) * std::max(rate, 0.0f);
        value = step > 0.0f
            ? previous.log2Exposure + (target - previous.log2Exposure) * (1.0f - std::exp(-step))
            : target;
    }
    next.log2Exposure = value;
    next.targetLog2Exposure = target;
    next.adapted = true;
    return next;
}

MeterState Settle(const std::vector<float>& frame, const ExposureSettings& settings) {
    return MirrorReduce(MirrorHistogram(frame), settings, {}, 1.0f / 60.0f);
}

// Runs `seconds` of a constant scene in steps of `deltaSeconds`.
MeterState Adapt(MeterState state, const Histogram& histogram, const ExposureSettings& settings,
                 float seconds, float deltaSeconds) {
    const int steps = static_cast<int>(std::lround(seconds / deltaSeconds));
    for (int i = 0; i < steps; ++i) state = MirrorReduce(histogram, settings, state, deltaSeconds);
    return state;
}

bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

using Config = Vixen::RenderGraph::LightingConfigNodeConfig;

ExposureSettings FromPreset(const Config::ExposurePreset& preset) {
    return {preset.keyValue, preset.minEV100, preset.maxEV100, preset.lowPercent,
            preset.highPercent, preset.speedUp, preset.speedDown};
}

ExposureSettings NamedPreset(const char* name) {
    const Config::ExposurePreset* preset = Config::FindExposurePreset(name);
    assert(preset != nullptr);
    return FromPreset(*preset);
}

void FirstFrameSnapsAndUniformSceneMetersItsLuminance() {
    // A uniform 0.18 frame meters EV100 = log2(0.18) + 3 to within half a bin and
    // maps to exposure 1 (key 0.18 / luminance 0.18); the first frame has no history.
    const MeterState state = Settle(std::vector<float>(64 * 64, 0.18f), {});
    assert(state.adapted);
    assert(Near(state.meteredEV100, std::log2(0.18f) + 3.0f, 0.125f));
    assert(Near(state.log2Exposure, 0.0f, 0.125f));
    assert(state.log2Exposure == state.targetLog2Exposure);
}

void LowPercentileRejectsDarkBackground() {
    // 80 % black background with 20 % lit subject at 0.25: a whole-frame log average
    // is dominated by the background, the 85..95 % window meters only the subject.
    std::vector<float> frame(100 * 100, 0.0f);
    std::fill(frame.begin(), frame.begin() + 2000, 0.25f);
    ExposureSettings settings;
    settings.lowPercent = 85.0f;
    settings.highPercent = 95.0f;
    settings.minEV100 = -32.0f;
    const MeterState state = Settle(frame, settings);
    assert(Near(state.meteredEV100, std::log2(0.25f) + 3.0f, 0.125f));
    // With no rejection the black pixels (bin 0) drag the meter to the histogram floor.
    settings.lowPercent = 0.0f;
    settings.highPercent = 100.0f;
    assert(Settle(frame, settings).meteredEV100 < state.meteredEV100 - 10.0f);
}

void HighPercentileRejectsSolarOutliers() {
    // 2 % of the frame is a sun at 2^11; a 95 % high cut ignores it entirely.
    std::vector<float> frame(100 * 100, 0.05f);
    std::fill(frame.begin(), frame.begin() + 200, 2048.0f);
    ExposureSettings settings;
    settings.lowPercent = 10.0f;
    settings.minEV100 = -32.0f;
    const MeterState withSun = Settle(frame, settings);
    const MeterState withoutSun = Settle(std::vector<float>(100 * 100, 0.05f), settings);
    assert(withSun.meteredEV100 == withoutSun.meteredEV100);
}

void HistogramIsOrderIndependent() {
    // Integer bins: any pixel order (any workgroup completion order) gives the same result.
    std::vector<float> frame;
    for (int i = 0; i < 4096; ++i) frame.push_back(std::exp2(static_cast<float>(i % 97) * 0.13f - 9.0f));
    std::vector<float> reversed(frame.rbegin(), frame.rend());
    assert(MirrorHistogram(frame) == MirrorHistogram(reversed));
    const MeterState a = Settle(frame, {});
    const MeterState b = Settle(reversed, {});
    assert(a.log2Exposure == b.log2Exposure && a.meteredEV100 == b.meteredEV100);
}

void FullFrameIsMeteredNotOneTile() {
    // Regression from the tile meter: the first 16x16 tile is black while the other
    // three are lit at 0.25. Every pixel reaches the histogram, so the lit 75 % sets
    // the meter, whereas a top-left-only meter would read only black.
    std::vector<float> blackCorner(32 * 32, 0.25f);
    for (uint32_t y = 0; y < 16; ++y)
        for (uint32_t x = 0; x < 16; ++x)
            blackCorner[y * 32 + x] = 0.0f;
    const Histogram histogram = MirrorHistogram(blackCorner);
    assert(histogram[0] == 256u && histogram[MirrorBin(0.25f)] == 768u);
    ExposureSettings settings;
    settings.lowPercent = 50.0f;
    settings.highPercent = 95.0f;
    settings.minEV100 = -32.0f;
    assert(Near(MirrorReduce(histogram, settings, {}, 0.0f).meteredEV100, std::log2(0.25f) + 3.0f, 0.125f));
    const std::vector<float> topLeftOnly(256, 0.0f);
    assert(MirrorReduce(MirrorHistogram(topLeftOnly), settings, {}, 0.0f).meteredEV100 < -10.0f);
}

void EV100ClampBoundsDarkAndBrightScenes() {
    // A near-black frame would need +17 EV; the default minimum caps the boost at
    // key / (2^(min - 3)), so a dark scene is never pushed to white.
    ExposureSettings settings;
    const MeterState dark = Settle(std::vector<float>(64 * 64, 1.0e-5f), settings);
    assert(dark.meteredEV100 < settings.minEV100);
    assert(Near(dark.targetLog2Exposure, std::log2(settings.keyValue) - (settings.minEV100 - 3.0f), 1e-5f));
    // A frame brighter than the maximum is held at the maximum rather than crushed further.
    settings.maxEV100 = 6.0f;
    const MeterState bright = Settle(std::vector<float>(64 * 64, 4096.0f), settings);
    assert(bright.meteredEV100 > settings.maxEV100);
    assert(Near(bright.targetLog2Exposure, std::log2(settings.keyValue) - (settings.maxEV100 - 3.0f), 1e-5f));
}

void AdaptationIsFrameRateIndependent() {
    // Darkening scene (exposure rises): 1 s at 30, 60, 144 and 240 Hz all land on
    // previous + (target - previous) * (1 - e^-speedDown).
    const ExposureSettings settings;
    const MeterState bright = Settle(std::vector<float>(64 * 64, 2.0f), settings);
    const Histogram darkScene = MirrorHistogram(std::vector<float>(64 * 64, 0.18f));
    const float target = MirrorReduce(darkScene, settings, {}, 0.0f).targetLog2Exposure;
    const float expected = bright.log2Exposure + (target - bright.log2Exposure) * (1.0f - std::exp(-1.0f));
    for (float hz : {30.0f, 60.0f, 144.0f, 240.0f}) {
        const MeterState after = Adapt(bright, darkScene, settings, 1.0f, 1.0f / hz);
        assert(Near(after.log2Exposure, expected, 2e-4f));
    }
    // One 1 s step equals sixty 1/60 s steps (the 0.5 s wall-clock cap is the node's, not the math's).
    assert(Near(MirrorReduce(darkScene, settings, bright, 1.0f).log2Exposure,
                Adapt(bright, darkScene, settings, 1.0f, 1.0f / 60.0f).log2Exposure, 2e-4f));
}

void BrightenAndDarkenUseTheirOwnSpeeds() {
    // Dark -> bright (exposure falls) uses speedUp = 3 /s; bright -> dark uses speedDown = 1 /s.
    const ExposureSettings settings;
    const Histogram dim = MirrorHistogram(std::vector<float>(64 * 64, 0.18f));
    const Histogram lit = MirrorHistogram(std::vector<float>(64 * 64, 2.88f));  // +4 EV
    const MeterState inDim = Settle(std::vector<float>(64 * 64, 0.18f), settings);
    const MeterState inLit = Settle(std::vector<float>(64 * 64, 2.88f), settings);
    const float gap = inDim.log2Exposure - inLit.log2Exposure;
    assert(Near(gap, 4.0f, 0.25f));
    const MeterState toLit = Adapt(inDim, lit, settings, 0.5f, 1.0f / 60.0f);
    const MeterState toDim = Adapt(inLit, dim, settings, 0.5f, 1.0f / 60.0f);
    const float remainingToLit = (toLit.log2Exposure - inLit.log2Exposure) / gap;
    const float remainingToDim = (inDim.log2Exposure - toDim.log2Exposure) / gap;
    assert(Near(remainingToLit, std::exp(-1.5f), 1e-3f));
    assert(Near(remainingToDim, std::exp(-0.5f), 1e-3f));
    // A zero step or zero rate snaps straight to the target.
    assert(MirrorReduce(lit, settings, inDim, 0.0f).log2Exposure == inLit.targetLog2Exposure);
}

void EmptyHistogramHoldsTheAdaptedValue() {
    const MeterState settled = Settle(std::vector<float>(64 * 64, 0.5f), {});
    const MeterState held = MirrorReduce(Histogram{}, {}, settled, 1.0f / 60.0f);
    assert(held.log2Exposure == settled.log2Exposure);
}

void PresetTableNamesSpaceAndInterior() {
    // R670.2: "dark" became "interior" (it lifts the most); "space" is distinct and lifts least.
    assert(Config::FindExposurePreset("dark") == nullptr);
    assert(Config::FindExposurePreset("nonexistent") == nullptr);
    const ExposureSettings ordinary = NamedPreset("default");
    const ExposureSettings interior = NamedPreset("interior");
    const ExposureSettings bright = NamedPreset("bright");
    const ExposureSettings space = NamedPreset("space");
    assert(interior.minEV100 < ordinary.minEV100 && ordinary.minEV100 < bright.minEV100);
    assert(bright.minEV100 < space.minEV100);
    assert(std::string_view(Config::EXPOSURE_PRESETS[0].name) == "default");
}

void SpacePresetLimitsTheLift() {
    // A near-black frame and a HUD-like frame (bodies on a navy sky, meters about -1.8 EV100).
    std::vector<float> navy(100 * 100, 0.03f);
    std::fill(navy.begin(), navy.begin() + 1500, 0.25f);
    const std::vector<float> nearBlack(64 * 64, 1.0e-5f);
    const ExposureSettings space = NamedPreset("space");
    const ExposureSettings ordinary = NamedPreset("default");
    const ExposureSettings interior = NamedPreset("interior");
    // Space never brightens beyond log2(key) - (floor - 3) = +0.026 EV: at most 1.02x.
    const float spaceCap = std::log2(space.keyValue) - (space.minEV100 - 3.0f);
    assert(spaceCap < 0.03f);
    const std::vector<float>* darkFrames[] = {&navy, &nearBlack};
    for (const std::vector<float>* frame : darkFrames) {
        const MeterState settled = Settle(*frame, space);
        assert(settled.meteredEV100 < space.minEV100);
        assert(Near(settled.log2Exposure, spaceCap, 1e-5f));
    }
    // The ordinary and interior presets lift the same frames far more (the round-2 HUD look).
    assert(Settle(navy, ordinary).log2Exposure > spaceCap + 1.5f);
    assert(Settle(nearBlack, ordinary).log2Exposure > spaceCap + 8.0f);
    assert(Settle(nearBlack, interior).log2Exposure > Settle(nearBlack, ordinary).log2Exposure + 3.0f);
    // Space only limits the lift: a frame above its floor meters exactly as the default does.
    const std::vector<float> lit(64 * 64, 0.5f);
    assert(Settle(lit, space).log2Exposure == Settle(lit, ordinary).log2Exposure);
}

void FixedExposureBypassesTheMeter() {
    // R670.1: a fixed EV100 exposes by key / 2^(EV100 - 3) whatever the frame holds, from the
    // first frame and with no adaptation lag after a scene change.
    const float fixedEV100 = -1.05893f;  // the planet scene's declaration: exposure 3
    const ExposureSettings fixed = FromPreset(Config::FixedExposure(fixedEV100));
    const float expected = std::log2(fixed.keyValue) - (fixedEV100 - 3.0f);
    assert(Near(expected, 1.585f, 1e-4f));
    std::vector<float> sun(100 * 100, 0.05f);
    std::fill(sun.begin(), sun.begin() + 200, 2048.0f);
    const std::vector<std::vector<float>> frames = {
        std::vector<float>(64 * 64, 1.0e-5f), std::vector<float>(64 * 64, 0.18f),
        std::vector<float>(64 * 64, 4096.0f), sun};
    for (const auto& frame : frames) {
        const MeterState first = Settle(frame, fixed);
        assert(first.log2Exposure == expected);
        for (const auto& next : frames) {
            const MeterState after = MirrorReduce(MirrorHistogram(next), fixed, first, 1.0f / 60.0f);
            assert(after.log2Exposure == expected);
        }
    }
    // The meter still reads the frame (for diagnostics); only its effect is bypassed.
    assert(Settle(frames[0], fixed).meteredEV100 < Settle(frames[2], fixed).meteredEV100);
}

}  // namespace

int main() {
    FirstFrameSnapsAndUniformSceneMetersItsLuminance();
    LowPercentileRejectsDarkBackground();
    HighPercentileRejectsSolarOutliers();
    HistogramIsOrderIndependent();
    FullFrameIsMeteredNotOneTile();
    EV100ClampBoundsDarkAndBrightScenes();
    AdaptationIsFrameRateIndependent();
    BrightenAndDarkenUseTheirOwnSpeeds();
    EmptyHistogramHoldsTheAdaptedValue();
    PresetTableNamesSpaceAndInterior();
    SpacePresetLimitsTheLift();
    FixedExposureBypassesTheMeter();
    return 0;
}
