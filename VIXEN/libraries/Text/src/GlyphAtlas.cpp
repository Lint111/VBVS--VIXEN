#include "Text/GlyphAtlas.h"

#include "Text/AtlasFormat.h"
#include "Recipe/generated/GlyphOutlineInstance.g.hpp"
#include "Recipe/generated/PathInstance.g.hpp"
#include "Recipe/generated/SdfCoreKernels.g.hpp"
#include "VixenHash.h"

#include FT_OUTLINE_H
#include <msdfgen.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Vixen::Text {

namespace {

struct OutlineBuilder {
    msdfgen::Shape shape;
    msdfgen::Contour* contour = nullptr;
    msdfgen::Point2 current{};
    msdfgen::Point2 start{};
};

msdfgen::Point2 ToPoint(const FT_Vector& point) {
    constexpr double kFreeTypePixelScale = 1.0 / 64.0;
    return {static_cast<double>(point.x) * kFreeTypePixelScale,
            static_cast<double>(point.y) * kFreeTypePixelScale};
}

msdfgen::Point2 ToPoint(const PathPoint& point, float pointsToPixels) {
    return {static_cast<double>(point.X) * static_cast<double>(pointsToPixels),
            static_cast<double>(point.Y) * static_cast<double>(pointsToPixels)};
}

void CloseContour(OutlineBuilder& builder) {
    if (builder.contour && builder.current != builder.start)
        builder.contour->addEdge(msdfgen::EdgeHolder(builder.current, builder.start));
}

int MoveTo(const FT_Vector* to, void* user) {
    auto& builder = *static_cast<OutlineBuilder*>(user);
    CloseContour(builder);
    builder.contour = &builder.shape.addContour();
    builder.current = ToPoint(*to);
    builder.start = builder.current;
    return 0;
}

int LineTo(const FT_Vector* to, void* user) {
    auto& builder = *static_cast<OutlineBuilder*>(user);
    const msdfgen::Point2 end = ToPoint(*to);
    if (builder.current != end)
        builder.contour->addEdge(msdfgen::EdgeHolder(builder.current, end));
    builder.current = end;
    return 0;
}

int ConicTo(const FT_Vector* control, const FT_Vector* to, void* user) {
    auto& builder = *static_cast<OutlineBuilder*>(user);
    const msdfgen::Point2 end = ToPoint(*to);
    builder.contour->addEdge(msdfgen::EdgeHolder(builder.current, ToPoint(*control), end));
    builder.current = end;
    return 0;
}

int CubicTo(const FT_Vector* control1, const FT_Vector* control2, const FT_Vector* to, void* user) {
    auto& builder = *static_cast<OutlineBuilder*>(user);
    const msdfgen::Point2 end = ToPoint(*to);
    builder.contour->addEdge(msdfgen::EdgeHolder(builder.current, ToPoint(*control1),
                                                  ToPoint(*control2), end));
    builder.current = end;
    return 0;
}

std::unique_ptr<msdfgen::Shape> BuildMsdfShapeFromBucket(const GlyphOutlineInstance& glyph,
                                                         float pointsToPixels) {
    auto shape = std::make_unique<msdfgen::Shape>();
    msdfgen::Contour* contour = nullptr;
    for (std::uint32_t index = 0; index < glyph.SegmentCount; ++index) {
        const PathSegment& segment = glyph.Segments[index];
        if (segment.ContourStart != 0U)
            contour = &shape->addContour();
        if (!contour)
            return {};

        const msdfgen::Point2 p0 = ToPoint(segment.P0, pointsToPixels);
        const msdfgen::Point2 p1 = ToPoint(segment.P1, pointsToPixels);
        const msdfgen::Point2 p2 = ToPoint(segment.P2, pointsToPixels);
        const msdfgen::Point2 p3 = ToPoint(segment.P3, pointsToPixels);
        switch (segment.Kind) {
            case 0U:
                if (p0 != p1)
                    contour->addEdge(msdfgen::EdgeHolder(p0, p1));
                break;
            case 1U:
                contour->addEdge(msdfgen::EdgeHolder(p0, p1, p2));
                break;
            case 2U:
                contour->addEdge(msdfgen::EdgeHolder(p0, p1, p2, p3));
                break;
            default:
                return {};
        }
    }
    shape->setYAxisOrientation(msdfgen::Y_UPWARD);
    shape->normalize();
    if (!shape->validate())
        return {};
    return shape;
}

constexpr FT_Outline_Funcs kOutlineCallbacks{
    &MoveTo,
    &LineTo,
    &ConicTo,
    &CubicTo,
    0,
    0};

std::uint8_t ToByte(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0f));
}

double DistanceInsideBox(double x, double y, double xMin, double yMin, double xMax, double yMax) {
    const double qx = std::abs(x - (xMin + xMax) * 0.5) - (xMax - xMin) * 0.5;
    const double qy = std::abs(y - (yMin + yMax) * 0.5) - (yMax - yMin) * 0.5;
    const double outside = std::hypot(std::max(qx, 0.0), std::max(qy, 0.0));
    const double inside = std::min(std::max(qx, qy), 0.0);
    return -(outside + inside);
}

} // namespace

struct GlyphAtlas::Impl {
    struct OutlineKey {
        FT_Face face = nullptr;
        std::uint32_t glyphIndex = 0;

        friend bool operator==(const OutlineKey&, const OutlineKey&) = default;
    };

    struct OutlineKeyHash {
        std::size_t operator()(const OutlineKey& key) const noexcept {
            const std::size_t faceHash = std::hash<FT_Face>{}(key.face);
            return faceHash ^ (static_cast<std::size_t>(key.glyphIndex) * 0x9e3779b9U);
        }
    };

    struct Placement {
        int x = 1;
        int y = 1;
        int shelfHeight = 0;
    };

    explicit Impl(std::uint32_t dimension, float range)
        : pageDimension(dimension), distanceRange(range), pixels(std::make_shared<std::vector<std::uint8_t>>(
              static_cast<std::size_t>(dimension) * dimension * 4, 0)) {
        if (pageDimension > 0) {
            for (std::size_t i = 3; i < pixels->size(); i += 4)
                (*pixels)[i] = 255;
            std::copy(std::begin(kAtlasFormatMarker), std::end(kAtlasFormatMarker), pixels->begin());
        }
    }

    bool Pack(int width, int height, int& x, int& y) {
        if (width <= 0 || height <= 0 || width + 2 > static_cast<int>(pageDimension) ||
            height + 2 > static_cast<int>(pageDimension))
            return false;
        if (placement.x + width + 1 > static_cast<int>(pageDimension)) {
            placement.x = 1;
            placement.y += placement.shelfHeight + 1;
            placement.shelfHeight = 0;
        }
        if (placement.y + height + 1 > static_cast<int>(pageDimension))
            return false;
        x = placement.x;
        y = placement.y;
        placement.x += width + 1;
        placement.shelfHeight = std::max(placement.shelfHeight, height);
        return true;
    }

    bool StoreGlyph(AtlasGlyph glyph, msdfgen::Shape* shape, double xMin, double yMin, double xMax, double yMax,
                    double outlineStroke = 0.0) {
        if (!(xMax > xMin) || !(yMax > yMin)) {
            glyphs.emplace(glyph.glyphIndex, glyph);
            return true;
        }

        if (outlineStroke <= 0.0) {
            if (!shape)
                return false;
            msdfgen::edgeColoringSimple(*shape, 3.0, static_cast<unsigned long long>(glyph.glyphIndex) + 1ULL);
        }

        const double pixelRange = static_cast<double>(distanceRange);
        const int width = static_cast<int>(std::ceil(xMax - xMin + 2.0 * pixelRange));
        const int height = static_cast<int>(std::ceil(yMax - yMin + 2.0 * pixelRange));
        int atlasX = 0;
        int atlasY = 0;
        if (!Pack(width, height, atlasX, atlasY))
            return false;

        if (currentSource) {
            retainedSources.push_back(std::move(currentSource));
            pixels = std::make_shared<std::vector<std::uint8_t>>(*pixels);
            std::copy(std::begin(kAtlasFormatMarker), std::end(kAtlasFormatMarker), pixels->begin());
        }

        msdfgen::Projection projection({1.0, 1.0}, {pixelRange - xMin, pixelRange - yMin});
        if (outlineStroke > 0.0) {
            // Preserve the tiny missing-glyph outline as a hollow ring. A direct box SDF
            // avoids contour-winding ambiguity, and identical RGB channels remain compatible
            // with the renderer's normal median-distance shader.
            for (int y = 0; y < height; ++y) {
                const int destinationY = atlasY + height - 1 - y;
                for (int x = 0; x < width; ++x) {
                    const double sampleX = xMin + static_cast<double>(x) + 0.5 - pixelRange;
                    const double sampleY = yMin + static_cast<double>(y) + 0.5 - pixelRange;
                    const double outerDistance = DistanceInsideBox(sampleX, sampleY, xMin, yMin, xMax, yMax);
                    const double innerDistance = DistanceInsideBox(sampleX, sampleY, xMin + outlineStroke,
                                                                    yMin + outlineStroke, xMax - outlineStroke,
                                                                    yMax - outlineStroke);
                    const double signedDistance = std::min(outerDistance, -innerDistance);
                    const std::uint8_t sample = ToByte(static_cast<float>(0.5 + signedDistance / (pixelRange * 2.0)));
                    const std::size_t destination =
                        (static_cast<std::size_t>(destinationY) * pageDimension + atlasX + x) * 4;
                    (*pixels)[destination + 0] = sample;
                    (*pixels)[destination + 1] = sample;
                    (*pixels)[destination + 2] = sample;
                    (*pixels)[destination + 3] = 255;
                }
            }
        } else {
            msdfgen::Bitmap<float, 3> field(width, height, msdfgen::Y_UPWARD);
            msdfgen::generateMSDF(field, *shape, projection, msdfgen::Range(pixelRange * 2.0));
            for (int y = 0; y < height; ++y) {
                const int destinationY = atlasY + height - 1 - y;
                for (int x = 0; x < width; ++x) {
                    const float* sample = field(x, y);
                    const std::size_t destination =
                        (static_cast<std::size_t>(destinationY) * pageDimension + atlasX + x) * 4;
                    (*pixels)[destination + 0] = ToByte(sample[0]);
                    (*pixels)[destination + 1] = ToByte(sample[1]);
                    (*pixels)[destination + 2] = ToByte(sample[2]);
                    (*pixels)[destination + 3] = 255;
                }
            }
        }

        glyph.padding = static_cast<float>(pixelRange);
        glyph.bearingX = static_cast<float>(xMin - pixelRange);
        glyph.bearingY = static_cast<float>(yMax + pixelRange);
        glyph.width = static_cast<float>(width);
        glyph.height = static_cast<float>(height);
        glyph.u0 = static_cast<float>(atlasX) / static_cast<float>(pageDimension);
        glyph.v0 = static_cast<float>(atlasY) / static_cast<float>(pageDimension);
        glyph.u1 = static_cast<float>(atlasX + width) / static_cast<float>(pageDimension);
        glyph.v1 = static_cast<float>(atlasY + height) / static_cast<float>(pageDimension);

        glyphs.emplace(glyph.glyphIndex, glyph);
        ++version;
        return true;
    }

    std::uint32_t pageDimension;
    float distanceRange;
    std::shared_ptr<std::vector<std::uint8_t>> pixels;
    Placement placement;
    std::uint32_t version = 1;
    std::unordered_map<std::uint32_t, AtlasGlyph> glyphs;
    std::unordered_map<OutlineKey, std::shared_ptr<const GlyphOutlineInstance>, OutlineKeyHash> outlineInstances;
    std::unordered_map<OutlineKey, std::uint32_t, OutlineKeyHash> outlineIndices;
    std::vector<GlyphOutlineInstance> outlineBlob;
    std::map<std::uint64_t, std::vector<std::uint32_t>> outlineHashBuckets;
    std::unique_ptr<Rml::CallbackTextureSource> currentSource;
    std::vector<std::unique_ptr<Rml::CallbackTextureSource>> retainedSources;

    std::uint32_t InternOutline(const GlyphOutlineInstance& outline) {
        const std::uint64_t hash = Vixen::Hash::ComputeHash64(&outline, sizeof(outline));
        auto& candidates = outlineHashBuckets[hash];
        for (const std::uint32_t index : candidates) {
            if (std::memcmp(&outlineBlob[index], &outline, sizeof(outline)) == 0)
                return index;
        }
        const std::uint32_t index = static_cast<std::uint32_t>(outlineBlob.size());
        outlineBlob.push_back(outline);
        candidates.push_back(index);
        return index;
    }
};

GlyphAtlas::GlyphAtlas(std::uint32_t pageDimension, float distanceRange)
    : impl_(std::make_unique<Impl>(pageDimension, distanceRange)) {}

GlyphAtlas::~GlyphAtlas() = default;

std::shared_ptr<const GlyphOutlineInstance> GlyphAtlas::GetOrAddOutline(
    FT_Face face, std::uint32_t glyphIndex, GlyphOutlineBakeDiagnostic* diagnostic) {
    const Impl::OutlineKey key{face, glyphIndex};
    const auto found = impl_->outlineInstances.find(key);
    if (found != impl_->outlineInstances.end()) {
        if (diagnostic) {
            diagnostic->glyphId = glyphIndex;
            diagnostic->code = GlyphOutlineBakeDiagnosticCode::None;
            diagnostic->requiredSegments = found->second->SegmentCount;
            diagnostic->segmentCapacity = static_cast<std::uint32_t>(Yeroket::Sdf::Generated::SegmentCapacity);
        }
        return found->second;
    }
    if (!face)
        return {};

    GlyphOutlineInstance instance{};
    GlyphOutlineBakeDiagnostic localDiagnostic;
    if (!LoadGlyphOutline(face, glyphIndex, instance, &localDiagnostic)) {
        if (diagnostic)
            *diagnostic = localDiagnostic;
        return {};
    }

    auto sharedInstance = std::make_shared<const GlyphOutlineInstance>(instance);
    const std::uint32_t outlineIndex = impl_->InternOutline(instance);
    impl_->outlineInstances.emplace(key, sharedInstance);
    impl_->outlineIndices.emplace(key, outlineIndex);
    if (diagnostic)
        *diagnostic = localDiagnostic;
    return sharedInstance;
}

std::uint32_t GlyphAtlas::GetOrAddOutlineIndex(
    FT_Face face, std::uint32_t glyphIndex, GlyphOutlineBakeDiagnostic* diagnostic) {
    const auto instance = GetOrAddOutline(face, glyphIndex, diagnostic);
    if (!instance)
        return std::numeric_limits<std::uint32_t>::max();
    const Impl::OutlineKey key{face, glyphIndex};
    const auto found = impl_->outlineIndices.find(key);
    return found == impl_->outlineIndices.end()
        ? std::numeric_limits<std::uint32_t>::max()
        : found->second;
}

std::span<const GlyphOutlineInstance> GlyphAtlas::OutlineBlob() const noexcept {
    return impl_->outlineBlob;
}

std::uint64_t GlyphAtlas::OutlineBlobBytes() const noexcept {
    return impl_->outlineBlob.size() * sizeof(GlyphOutlineInstance);
}

bool GlyphAtlas::AddGlyph(FT_Face face, std::uint32_t glyphIndex, int fontSize) {
    if (!face || fontSize <= 0 || impl_->pageDimension == 0 || impl_->distanceRange <= 0.0f)
        return false;
    if (impl_->glyphs.contains(glyphIndex))
        return true;
    if (glyphIndex == 0)
        return false;

    if (FT_Set_Char_Size(face, 0, static_cast<FT_F26Dot6>(fontSize) * 64, 0, 0) != 0 ||
        FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP) != 0)
        return false;

    AtlasGlyph glyph;
    glyph.glyphIndex = glyphIndex;
    const FT_GlyphSlot slot = face->glyph;
    glyph.advance = static_cast<float>(slot->advance.x) / 64.0f;
    if (slot->format != FT_GLYPH_FORMAT_OUTLINE || slot->outline.n_contours == 0 || slot->outline.n_points == 0) {
        impl_->glyphs.emplace(glyphIndex, glyph);
        return true;
    }

    OutlineBuilder builder;
    if (FT_Outline_Decompose(&slot->outline, &kOutlineCallbacks, &builder) != 0)
        return false;
    CloseContour(builder);
    builder.shape.setYAxisOrientation(msdfgen::Y_UPWARD);
    builder.shape.normalize();
    if (!builder.shape.validate())
        return false;

    constexpr double kFreeTypePixelScale = 1.0 / 64.0;
    const double xMin = static_cast<double>(slot->metrics.horiBearingX) * kFreeTypePixelScale;
    const double yMax = static_cast<double>(slot->metrics.horiBearingY) * kFreeTypePixelScale;
    const double xMax = xMin + static_cast<double>(slot->metrics.width) * kFreeTypePixelScale;
    const double yMin = yMax - static_cast<double>(slot->metrics.height) * kFreeTypePixelScale;
    return impl_->StoreGlyph(glyph, &builder.shape, xMin, yMin, xMax, yMax);
}

bool GlyphAtlas::AddGlyphFromOutline(std::uint32_t glyphIndex, const GlyphOutlineInstance& outline,
                                     float pointsToPixels) {
    if (glyphIndex == 0 || impl_->pageDimension == 0 || impl_->distanceRange <= 0.0f ||
        outline.SegmentCount > static_cast<std::uint32_t>(Yeroket::Sdf::Generated::SegmentCapacity) ||
        !std::isfinite(pointsToPixels) || pointsToPixels <= 0.0f)
        return false;
    if (impl_->glyphs.contains(glyphIndex))
        return true;

    AtlasGlyph glyph;
    glyph.glyphIndex = glyphIndex;
    if (outline.SegmentCount == 0U) {
        impl_->glyphs.emplace(glyphIndex, glyph);
        return true;
    }

    std::unique_ptr<msdfgen::Shape> shape = BuildMsdfShapeFromBucket(outline, pointsToPixels);
    if (!shape)
        return false;

    const double scale = static_cast<double>(pointsToPixels);
    const double xMin = static_cast<double>(outline.BoundsMin.X) * scale;
    const double yMin = static_cast<double>(outline.BoundsMin.Y) * scale;
    const double xMax = static_cast<double>(outline.BoundsMax.X) * scale;
    const double yMax = static_cast<double>(outline.BoundsMax.Y) * scale;
    return impl_->StoreGlyph(glyph, shape.get(), xMin, yMin, xMax, yMax);
}

bool GlyphAtlas::AddReplacementGlyph(FT_Face face, int fontSize) {
    if (!face || fontSize <= 0 || impl_->pageDimension == 0 || impl_->distanceRange <= 0.0f)
        return false;
    if (impl_->glyphs.contains(0))
        return true;

    const double xMin = 1.0;
    const double yMin = 0.0;
    const double xMax = xMin + static_cast<double>(std::max(fontSize / 3, 1));
    const double yMax = static_cast<double>((fontSize * 2) / 3);
    const double stroke = 1.0;
    if (!(yMax > yMin) || xMax - xMin <= 2.0 * stroke || yMax - yMin <= 2.0 * stroke)
        return false;

    AtlasGlyph glyph;
    glyph.glyphIndex = 0;
    return impl_->StoreGlyph(glyph, nullptr, xMin, yMin, xMax, yMax, stroke);
}

const AtlasGlyph* GlyphAtlas::FindGlyph(std::uint32_t glyphIndex) const {
    const auto it = impl_->glyphs.find(glyphIndex);
    return it == impl_->glyphs.end() ? nullptr : &it->second;
}

Rml::Texture GlyphAtlas::GetTexture(Rml::RenderManager& renderManager) const {
    if (!impl_->currentSource) {
        const auto pixels = std::shared_ptr<const std::vector<std::uint8_t>>(impl_->pixels);
        const auto dimension = static_cast<int>(impl_->pageDimension);
        auto source = std::make_unique<Rml::CallbackTextureSource>(
            [pixels, dimension](const Rml::CallbackTextureInterface& textureInterface) {
                return textureInterface.GenerateTexture(
                    Rml::Span<const Rml::byte>(reinterpret_cast<const Rml::byte*>(pixels->data()), pixels->size()),
                    Rml::Vector2i(dimension, dimension));
            });
        impl_->currentSource = std::move(source);
    }
    return impl_->currentSource->GetTexture(renderManager);
}

std::uint32_t GlyphAtlas::Version() const {
    return impl_->version;
}

std::span<const std::uint8_t> GlyphAtlas::Pixels() const noexcept {
    return *impl_->pixels;
}

std::uint32_t GlyphAtlas::PageDimension() const noexcept {
    return impl_->pageDimension;
}

float GlyphAtlas::DistanceRange() const noexcept {
    return impl_->distanceRange;
}

} // namespace Vixen::Text
