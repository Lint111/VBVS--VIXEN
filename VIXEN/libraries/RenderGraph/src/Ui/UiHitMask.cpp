// UiHitMask.cpp — implementation of the per-element UI hit-mask math (see Ui/UiHitMask.h).
//
// Pure CPU. The only "heavy" dependency is the header-only stb_image (used solely for the optional
// Image mask form); its IMPLEMENTATION is compiled exactly once in VulkanResources' STBTextureLoader,
// so here we include the header for the stbi_load DECLARATION only (no STB_IMAGE_IMPLEMENTATION).

#include "Ui/UiHitMask.h"

#include <stb_image.h>

#include <algorithm>  // std::min, std::max, std::transform
#include <cctype>     // std::tolower, std::isspace
#include <cstdint>
#include <cstdlib>    // std::strtof
#include <iostream>   // std::cerr (one-time fail-open log on image-mask load failure)
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Vixen::RenderGraph {

namespace {

// ---- small string helpers (local, header-free) ------------------------------------------------

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// ---- image-mask cache (lazy, path-keyed, thread-safe) -----------------------------------------
//
// A loaded mask: tightly-packed RGBA (4 bytes/px), width/height in px, and a `loaded` flag. A failed
// load is cached as { loaded=false } so we neither retry every frame nor re-log; a missing mask is
// treated as Box (fail-open) by the caller.
struct CachedMask {
    bool                 loaded = false;
    int                  width  = 0;
    int                  height = 0;
    std::vector<uint8_t> rgba;  // size == width*height*4 when loaded
};

std::mutex&                                    MaskCacheMutex() {
    static std::mutex m;
    return m;
}
std::unordered_map<std::string, CachedMask>&   MaskCache() {
    static std::unordered_map<std::string, CachedMask> cache;
    return cache;
}
std::unordered_set<std::string>&               LoggedFailures() {
    static std::unordered_set<std::string> logged;
    return logged;
}

// Return the cached mask for `path`, loading it on first request. Caller holds no lock; this takes
// the cache lock internally. The returned pointer is stable for the process lifetime (entries are
// never erased), so reading it outside the lock is safe.
const CachedMask* GetOrLoadMask(const std::string& path) {
    {
        std::lock_guard<std::mutex> lock(MaskCacheMutex());
        auto it = MaskCache().find(path);
        if (it != MaskCache().end()) {
            return &it->second;
        }
    }

    // Load OUTSIDE the lock (stbi_load can be slow); then publish under the lock. A concurrent loader
    // for the same path would also load — harmless (last writer wins, same bytes).
    CachedMask mask;
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (pixels && w > 0 && h > 0) {
        mask.loaded = true;
        mask.width  = w;
        mask.height = h;
        mask.rgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
    }
    if (pixels) {
        stbi_image_free(pixels);
    }

    if (!mask.loaded) {
        std::lock_guard<std::mutex> lock(MaskCacheMutex());
        if (LoggedFailures().insert(path).second) {
            std::cerr << "[UiHitMask] hit-mask image failed to load (treating as box, fail-open): "
                      << path << '\n';
        }
    }

    std::lock_guard<std::mutex> lock(MaskCacheMutex());
    auto [it, inserted] = MaskCache().emplace(path, std::move(mask));
    (void)inserted;
    return &it->second;
}

// ---- shape tests ------------------------------------------------------------------------------

bool EllipseContains(float x, float y, float w, float h) {
    const float rx = w * 0.5f;
    const float ry = h * 0.5f;
    if (rx <= 0.0f || ry <= 0.0f) return false;
    const float nx = (x - rx) / rx;
    const float ny = (y - ry) / ry;
    return (nx * nx + ny * ny) <= 1.0f;
}

bool RoundedRectContains(float x, float y, float w, float h, float radius) {
    // Clamp the corner radius so opposite corners never overlap.
    const float r = std::max(0.0f, std::min(radius, std::min(w, h) * 0.5f));
    if (r <= 0.0f) return true;  // degenerates to a plain box (AABB already passed)

    // Distance from the point to the inner rectangle [r, w-r] x [r, h-r]. In an edge band this is
    // zero (or along a single axis, <= r since the box passed), so it's inside; only when the point
    // is past the inner rect in BOTH axes (a corner quadrant) does it measure the corner arc.
    const float cx = std::max(r, std::min(x, w - r));
    const float cy = std::max(r, std::min(y, h - r));
    const float dx = x - cx;
    const float dy = y - cy;
    return (dx * dx + dy * dy) <= (r * r);
}

bool ImageContains(const HitMaskSpec& spec, float x, float y, float w, float h) {
    const CachedMask* mask = GetOrLoadMask(spec.imagePath);
    if (!mask || !mask->loaded) {
        return true;  // fail-open: an unloadable mask behaves as a plain box.
    }
    // Map element-local (0..w, 0..h) → mask texel. Nearest-sample; clamp to the texel grid.
    int tx = static_cast<int>((x / w) * static_cast<float>(mask->width));
    int ty = static_cast<int>((y / h) * static_cast<float>(mask->height));
    tx = std::max(0, std::min(tx, mask->width - 1));
    ty = std::max(0, std::min(ty, mask->height - 1));
    const size_t idx = (static_cast<size_t>(ty) * mask->width + tx) * 4 + 3;  // +3 = alpha channel
    return mask->rgba[idx] >= 128;
}

} // namespace

// ---- public API -------------------------------------------------------------------------------

HitMaskSpec ParseHitMask(const std::string& attr) {
    HitMaskSpec spec;  // defaults to Box
    const std::string trimmed = Trim(attr);
    if (trimmed.empty()) {
        return spec;  // "" → Box
    }

    const std::string lower = ToLower(trimmed);

    // url(path) → Image. Extract the path between the parentheses, then strip optional quotes.
    if (lower.rfind("url(", 0) == 0) {
        const size_t open = trimmed.find('(');
        const size_t close = trimmed.rfind(')');
        if (open != std::string::npos && close != std::string::npos && close > open) {
            std::string path = Trim(trimmed.substr(open + 1, close - open - 1));
            if (path.size() >= 2 &&
                ((path.front() == '"' && path.back() == '"') ||
                 (path.front() == '\'' && path.back() == '\''))) {
                path = path.substr(1, path.size() - 2);
            }
            if (!path.empty()) {
                spec.shape = HitMaskShape::Image;
                spec.imagePath = path;
                return spec;
            }
        }
        return spec;  // malformed url(...) → Box (fail-open)
    }

    if (lower == "none" || lower == "box") {
        return spec;  // Box
    }

    if (lower == "ellipse") {
        spec.shape = HitMaskShape::Ellipse;
        return spec;
    }

    // "rounded-rect" or "rounded-rect <px>" (whitespace-separated optional radius).
    if (lower.rfind("rounded-rect", 0) == 0) {
        spec.shape = HitMaskShape::RoundedRect;
        spec.radius = kDefaultRoundedRectRadius;
        const std::string rest = Trim(trimmed.substr(std::string("rounded-rect").size()));
        if (!rest.empty()) {
            const char* begin = rest.c_str();
            char* end = nullptr;
            const float parsed = std::strtof(begin, &end);
            if (end != begin && parsed > 0.0f) {
                spec.radius = parsed;
            }
        }
        return spec;
    }

    return spec;  // unrecognised → Box (fail-open)
}

bool HitMaskContains(const HitMaskSpec& spec,
                     float localX, float localY,
                     float width, float height) {
    if (width <= 0.0f || height <= 0.0f) {
        return false;  // degenerate element box
    }
    // Defensive: the caller pre-checks the AABB, but never trust a pixel outside [0,w]x[0,h].
    if (localX < 0.0f || localY < 0.0f || localX > width || localY > height) {
        return false;
    }

    switch (spec.shape) {
        case HitMaskShape::Box:
            return true;
        case HitMaskShape::Ellipse:
            return EllipseContains(localX, localY, width, height);
        case HitMaskShape::RoundedRect:
            return RoundedRectContains(localX, localY, width, height, spec.radius);
        case HitMaskShape::Image:
            return ImageContains(spec, localX, localY, width, height);
    }
    return true;  // unreachable; fail-open
}

} // namespace Vixen::RenderGraph
