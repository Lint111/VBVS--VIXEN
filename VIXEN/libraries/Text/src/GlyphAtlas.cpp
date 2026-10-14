#include "Text/GlyphAtlas.h"

#include "Text/AtlasFormat.h"

#include FT_OUTLINE_H
#include <msdfgen.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
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
    return {static_cast<double>(point.x), static_cast<double>(point.y)};
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
    builder.contour->addEdge(msdfgen::EdgeHolder(builder.current, ToPoint(*control1), ToPoint(*control2), end));
    builder.current = end;
    return 0;
}

std::uint8_t ToByte(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0f));
}

void AddRectangleContour(msdfgen::Shape& shape, const std::array<msdfgen::Point2, 4>& points) {
    msdfgen::Contour& contour = shape.addContour();
    for (std::size_t i = 0; i < points.size(); ++i)
        contour.addEdge(msdfgen::EdgeHolder(points[i], points[(i + 1) % points.size()]));
}

} // namespace

struct GlyphAtlas::Impl {
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

    bool StoreGlyph(AtlasGlyph glyph, FT_Face face, msdfgen::Shape& shape,
                    double xMin, double yMin, double xMax, double yMax) {
        if (!(xMax > xMin) || !(yMax > yMin)) {
            glyphs.emplace(glyph.glyphIndex, glyph);
            return true;
        }

        msdfgen::edgeColoringSimple(shape, 3.0, static_cast<unsigned long long>(glyph.glyphIndex) + 1ULL);

        const double emScale = static_cast<double>(kAtlasDesignPixelsPerEm) / static_cast<double>(face->units_per_EM);
        const double pixelRange = static_cast<double>(distanceRange);
        const int width = static_cast<int>(std::ceil((xMax - xMin) * emScale + 2.0 * pixelRange));
        const int height = static_cast<int>(std::ceil((yMax - yMin) * emScale + 2.0 * pixelRange));
        int atlasX = 0;
        int atlasY = 0;
        if (!Pack(width, height, atlasX, atlasY))
            return false;

        if (currentSource) {
            retainedSources.push_back(std::move(currentSource));
            pixels = std::make_shared<std::vector<std::uint8_t>>(*pixels);
            std::copy(std::begin(kAtlasFormatMarker), std::end(kAtlasFormatMarker), pixels->begin());
        }

        msdfgen::Bitmap<float, 3> field(width, height, msdfgen::Y_UPWARD);
        msdfgen::Projection projection(
            {emScale, emScale},
            {pixelRange - xMin * emScale, pixelRange - yMin * emScale});
        msdfgen::generateMSDF(field, shape, projection, msdfgen::Range(pixelRange * 2.0));

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

        const double rangeInFontUnits = pixelRange * static_cast<double>(face->units_per_EM) /
                                        static_cast<double>(kAtlasDesignPixelsPerEm);
        glyph.padding = static_cast<float>(pixelRange / static_cast<double>(kAtlasDesignPixelsPerEm));
        glyph.bearingX = static_cast<float>((xMin - rangeInFontUnits) / face->units_per_EM);
        glyph.bearingY = static_cast<float>((yMax + rangeInFontUnits) / face->units_per_EM);
        glyph.width = static_cast<float>(width) / static_cast<float>(kAtlasDesignPixelsPerEm);
        glyph.height = static_cast<float>(height) / static_cast<float>(kAtlasDesignPixelsPerEm);
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
    std::unique_ptr<Rml::CallbackTextureSource> currentSource;
    std::vector<std::unique_ptr<Rml::CallbackTextureSource>> retainedSources;
};

GlyphAtlas::GlyphAtlas(std::uint32_t pageDimension, float distanceRange)
    : impl_(std::make_unique<Impl>(pageDimension, distanceRange)) {}

GlyphAtlas::~GlyphAtlas() = default;

bool GlyphAtlas::AddGlyph(FT_Face face, std::uint32_t glyphIndex) {
    if (!face || face->units_per_EM == 0 || impl_->pageDimension == 0 || impl_->distanceRange <= 0.0f)
        return false;
    if (impl_->glyphs.contains(glyphIndex))
        return true;
    if (glyphIndex == 0)
        return false;

    constexpr FT_Int32 loadFlags = FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP;
    if (FT_Load_Glyph(face, glyphIndex, loadFlags) != 0)
        return false;

    AtlasGlyph glyph;
    glyph.glyphIndex = glyphIndex;
    const FT_GlyphSlot slot = face->glyph;
    glyph.advance = static_cast<float>(slot->advance.x) / static_cast<float>(face->units_per_EM);
    if (slot->format != FT_GLYPH_FORMAT_OUTLINE || slot->outline.n_contours == 0 || slot->outline.n_points == 0) {
        impl_->glyphs.emplace(glyphIndex, glyph);
        return true;
    }

    OutlineBuilder builder;
    FT_Outline_Funcs functions{};
    functions.move_to = MoveTo;
    functions.line_to = LineTo;
    functions.conic_to = ConicTo;
    functions.cubic_to = CubicTo;
    functions.shift = 0;
    functions.delta = 0;
    if (FT_Outline_Decompose(&slot->outline, &functions, &builder) != 0)
        return false;
    CloseContour(builder);
    builder.shape.setYAxisOrientation(msdfgen::Y_UPWARD);
    builder.shape.normalize();
    if (!builder.shape.validate())
        return false;

    const double xMin = static_cast<double>(slot->metrics.horiBearingX);
    const double yMax = static_cast<double>(slot->metrics.horiBearingY);
    const double xMax = xMin + static_cast<double>(slot->metrics.width);
    const double yMin = yMax - static_cast<double>(slot->metrics.height);
    return impl_->StoreGlyph(glyph, face, builder.shape, xMin, yMin, xMax, yMax);
}

bool GlyphAtlas::AddReplacementGlyph(FT_Face face, int fontSize) {
    if (!face || face->units_per_EM == 0 || fontSize <= 0 || impl_->pageDimension == 0 ||
        impl_->distanceRange <= 0.0f)
        return false;
    if (impl_->glyphs.contains(0))
        return true;

    const double fontUnitsPerPixel = static_cast<double>(face->units_per_EM) / static_cast<double>(fontSize);
    const double xMin = fontUnitsPerPixel;
    const double yMin = 0.0;
    const double xMax = xMin + static_cast<double>(std::max(fontSize / 3, 1)) * fontUnitsPerPixel;
    const double yMax = static_cast<double>((fontSize * 2) / 3) * fontUnitsPerPixel;
    const double stroke = fontUnitsPerPixel;
    if (!(yMax > yMin) || xMax - xMin <= 2.0 * stroke || yMax - yMin <= 2.0 * stroke)
        return false;

    msdfgen::Shape shape;
    AddRectangleContour(shape, {{{xMin, yMin}, {xMax, yMin}, {xMax, yMax}, {xMin, yMax}}});
    AddRectangleContour(shape, {{{xMin + stroke, yMin + stroke}, {xMin + stroke, yMax - stroke},
                                 {xMax - stroke, yMax - stroke}, {xMax - stroke, yMin + stroke}}});
    shape.setYAxisOrientation(msdfgen::Y_UPWARD);
    shape.normalize();
    if (!shape.validate())
        return false;

    AtlasGlyph glyph;
    glyph.glyphIndex = 0;
    return impl_->StoreGlyph(glyph, face, shape, xMin, yMin, xMax, yMax);
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

} // namespace Vixen::Text
