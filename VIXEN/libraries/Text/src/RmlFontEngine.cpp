#include "Text/RmlFontEngine.h"

#include "Text/Abi.h"
#include "Text/GlyphAtlas.h"
#include "Text/Shaper.h"

#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Vixen::Text {

namespace {

constexpr const char* kBundledLatoFamily = "LatoLatin";
constexpr FT_Int32 kSizedGlyphLoadFlags = FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP;

struct FontFace {
    FT_Face face = nullptr;
    std::string path;
    Rml::String family;
    Rml::Style::FontStyle style = Rml::Style::FontStyle::Normal;
    Rml::Style::FontWeight weight = Rml::Style::FontWeight::Normal;
    bool fallback = false;
    ~FontFace() {
        if (face)
            FT_Done_Face(face);
    }
};

struct FontHandle {
    FontFace* font = nullptr;
    int size = 16;
    Rml::FontMetrics metrics{};
    GlyphAtlas atlas;
};

float MissingGlyphAdvance(int fontSize) {
    return static_cast<float>(std::max(fontSize / 3, 1) + 2);
}

FontHandle* FromHandle(Rml::FontFaceHandle handle) {
    return reinterpret_cast<FontHandle*>(handle);
}

int RoundWidth(float width) {
    return std::max(0, static_cast<int>(std::lround(width)));
}

FT_UInt GlyphIndex(FT_Face face, Rml::Character character) {
    return FT_Get_Char_Index(face, static_cast<FT_ULong>(character));
}

bool SetFontSize(FT_Face face, int size) {
    if (!face || size <= 0)
        return false;
    return FT_Set_Char_Size(face, 0, static_cast<FT_F26Dot6>(size) * 64, 0, 0) == 0;
}

float MeasureWidth(FontHandle& handle, Rml::StringView string, const Rml::TextShapingContext& context,
                   Rml::Character priorCharacter) {
    if (!handle.font || !handle.font->face)
        return 0.0f;

    FT_Face face = handle.font->face;
    if (!SetFontSize(face, handle.size))
        return 0.0f;

    int advance = 0;
    FT_UInt previousGlyph = priorCharacter == Rml::Character::Null ? 0 : GlyphIndex(face, priorCharacter);
    const bool kerningEnabled = context.font_kerning != Rml::Style::FontKerning::None && FT_HAS_KERNING(face);

    for (auto it = Rml::StringIteratorU8(string); it; ++it) {
        const FT_UInt glyphIndex = GlyphIndex(face, *it);
        if (kerningEnabled && previousGlyph != 0 && glyphIndex != 0) {
            FT_Vector kerning{};
            if (FT_Get_Kerning(face, previousGlyph, glyphIndex, FT_KERNING_DEFAULT, &kerning) == 0)
                advance += static_cast<int>(kerning.x >> 6);
        }
        if (glyphIndex == 0)
            advance += static_cast<int>(MissingGlyphAdvance(handle.size));
        else if (FT_Load_Glyph(face, glyphIndex, kSizedGlyphLoadFlags) == 0)
            advance += static_cast<int>(face->glyph->metrics.horiAdvance >> 6);
        advance += static_cast<int>(context.letter_spacing);
        previousGlyph = glyphIndex;
    }
    return static_cast<float>(std::max(advance, 0));
}

bool IsBundledFacePath(const Rml::String& fileName) {
    std::error_code error;
    const std::filesystem::path path = std::filesystem::weakly_canonical(fileName, error);
    if (error)
        return false;
    const std::string extension = path.extension().string();
    if (extension != ".ttf" && extension != ".otf" && extension != ".ttc")
        return false;
    return path.generic_string().find("/assets/ui/") != std::string::npos;
}

} // namespace

struct RmlFontEngine::Impl {
    FT_Library library = nullptr;
    std::vector<std::unique_ptr<FontFace>> faces;
    std::vector<std::unique_ptr<FontHandle>> handles;
    std::unordered_map<std::uint64_t, FontHandle*> handleLookup;
    std::optional<PresetDesc> preset;
    std::vector<std::uint32_t> requiredScriptTags;
    std::string language;
    bool initialized = false;

    FontHandle* GetOrCreateHandle(FontFace* face, int size) {
        const std::uint64_t key = (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(face)) << 1) ^
                                  static_cast<std::uint32_t>(std::max(size, 1));
        if (auto it = handleLookup.find(key); it != handleLookup.end())
            return it->second;

        auto handle = std::make_unique<FontHandle>();
        handle->font = face;
        handle->size = std::max(size, 1);
        FT_Face ftFace = face->face;
        handle->metrics.size = handle->size;
        handle->metrics.has_ellipsis = FT_Get_Char_Index(ftFace, 0x2026) != 0;
        if (SetFontSize(ftFace, handle->size)) {
            const FT_Size_Metrics& sizeMetrics = ftFace->size->metrics;
            handle->metrics.ascent = static_cast<float>(sizeMetrics.ascender) / 64.0f;
            handle->metrics.descent = static_cast<float>(-sizeMetrics.descender) / 64.0f;
            handle->metrics.line_spacing = static_cast<float>(sizeMetrics.height) / 64.0f;
            handle->metrics.underline_position = std::max(
                0.0f, static_cast<float>(FT_MulFix(-ftFace->underline_position, sizeMetrics.y_scale)) / 64.0f);
            handle->metrics.underline_thickness = std::max(
                1.0f, static_cast<float>(FT_MulFix(ftFace->underline_thickness, sizeMetrics.y_scale)) / 64.0f);
            const FT_UInt xGlyph = FT_Get_Char_Index(ftFace, static_cast<FT_ULong>('x'));
            if (xGlyph != 0 && FT_Load_Glyph(ftFace, xGlyph, kSizedGlyphLoadFlags) == 0)
                handle->metrics.x_height = static_cast<float>(ftFace->glyph->metrics.height) / 64.0f;
            else
                handle->metrics.x_height = 0.5f * handle->metrics.line_spacing;
        }

        FontHandle* result = handle.get();
        handles.push_back(std::move(handle));
        handleLookup.emplace(key, result);
        return result;
    }

    FontFace* FindFace(const Rml::String& family, Rml::Style::FontStyle style, Rml::Style::FontWeight weight) const {
        FontFace* fallback = nullptr;
        for (const auto& candidate : faces) {
            if (candidate->style == style &&
                Rml::StringUtilities::StringCompareCaseInsensitive(candidate->family, family)) {
                if (candidate->weight == weight || weight == Rml::Style::FontWeight::Auto)
                    return candidate.get();
                if (!fallback)
                    fallback = candidate.get();
            }
            if (candidate->fallback && !fallback)
                fallback = candidate.get();
        }
        return fallback;
    }
};

RmlFontEngine::RmlFontEngine() : impl_(std::make_unique<Impl>()) {}

RmlFontEngine::~RmlFontEngine() = default;

void RmlFontEngine::SetPresetDesc(const PresetDesc& preset,
                                 std::span<const std::uint32_t> requiredScriptTags,
                                 std::string_view language) {
    impl_->preset = preset;
    impl_->requiredScriptTags.assign(requiredScriptTags.begin(), requiredScriptTags.end());
    impl_->language.assign(language.begin(), language.end());
}

void RmlFontEngine::ClearPresetDesc() {
    impl_->preset.reset();
    impl_->requiredScriptTags.clear();
    impl_->language.clear();
}

int RmlFontEngine::BuildGlyphInstances(Rml::FontFaceHandle faceHandle, Rml::StringView string,
                                       Rml::Vector2f baseline, std::uint32_t span,
                                       float letterSpacing, std::vector<GlyphInstance>& output) {
    FontHandle* handle = FromHandle(faceHandle);
    if (!handle || !handle->font || !handle->font->face || !SetFontSize(handle->font->face, handle->size))
        return 0;

    FT_Face face = handle->font->face;
    GlyphAtlas& atlas = handle->atlas;
    float penX = baseline.x;
    float penY = baseline.y;
    auto append = [&](std::uint32_t glyphIndex, std::uint32_t cluster, float offsetX, float offsetY) {
        const bool added = glyphIndex == 0 ? atlas.AddReplacementGlyph(face, handle->size)
                                           : atlas.AddGlyph(face, glyphIndex, handle->size);
        if (!added) return;
        const AtlasGlyph* glyph = atlas.FindGlyph(glyphIndex);
        if (!glyph || glyph->width <= 0.0f || glyph->height <= 0.0f) return;

        const float padding = glyph->padding;
        const float leftBearing = std::floor(glyph->bearingX + padding);
        const float topBearing = std::ceil(glyph->bearingY - padding);
        const float x = penX + offsetX + leftBearing - padding;
        const float y = penY - offsetY - topBearing - padding;
        output.push_back({x, y, glyph->width, glyph->height,
                          glyph->u0, glyph->v0, glyph->u1, glyph->v1,
                          span, cluster, atlas.DistanceRange(), 0u});
    };

    if (impl_->preset) {
        const ShapedText shaped = ShapeUtf8(*impl_->preset, impl_->requiredScriptTags, face,
                                            std::string_view(string.begin(), string.size()), impl_->language);
        if (!shaped.visualRuns.empty()) {
            std::unordered_map<std::uint32_t, std::uint32_t> clusterOrdinals;
            std::uint32_t nextCluster = 0;
            for (const ShapedRun& run : shaped.visualRuns) {
                for (std::size_t index = 0; index < run.glyphs.size(); ++index) {
                    const ShapedGlyph& glyph = run.glyphs[index];
                    const auto [ordinal, inserted] = clusterOrdinals.emplace(glyph.cluster, nextCluster);
                    if (inserted) ++nextCluster;
                    append(glyph.glyphId, ordinal->second,
                           static_cast<float>(glyph.xOffset) / 64.0f,
                           static_cast<float>(glyph.yOffset) / 64.0f);
                    penX += static_cast<float>(glyph.xAdvance) / 64.0f;
                    penY -= static_cast<float>(glyph.yAdvance) / 64.0f;
                    if (index + 1 == run.glyphs.size() || run.glyphs[index + 1].cluster != glyph.cluster)
                        penX += letterSpacing;
                }
            }
            return RoundWidth(penX - baseline.x);
        }
    }

    std::uint32_t cluster = 0;
    FT_UInt previousGlyph = 0;
    const bool kerningEnabled = FT_HAS_KERNING(face);
    for (auto it = Rml::StringIteratorU8(string); it; ++it, ++cluster) {
        const FT_UInt glyphIndex = GlyphIndex(face, *it);
        if (kerningEnabled && previousGlyph != 0 && glyphIndex != 0) {
            FT_Vector kerning{};
            if (FT_Get_Kerning(face, previousGlyph, glyphIndex, FT_KERNING_DEFAULT, &kerning) == 0)
                penX += static_cast<float>(kerning.x >> 6);
        }

        float advance = MissingGlyphAdvance(handle->size);
        if (glyphIndex != 0 && FT_Load_Glyph(face, glyphIndex, kSizedGlyphLoadFlags) == 0)
            advance = static_cast<float>(face->glyph->metrics.horiAdvance >> 6);
        append(glyphIndex, cluster, 0.0f, 0.0f);
        penX += advance + letterSpacing;
        previousGlyph = glyphIndex;
    }
    return RoundWidth(penX - baseline.x);
}

GlyphAtlas* RmlFontEngine::GetGlyphAtlas(Rml::FontFaceHandle faceHandle) const {
    FontHandle* handle = FromHandle(faceHandle);
    return handle ? &handle->atlas : nullptr;
}

void RmlFontEngine::Initialize() {
    if (impl_->initialized)
        return;
    impl_->initialized = FT_Init_FreeType(&impl_->library) == 0;
}

void RmlFontEngine::Shutdown() {
    ReleaseFontResources();
    impl_->faces.clear();
    if (impl_->library) {
        FT_Done_FreeType(impl_->library);
        impl_->library = nullptr;
    }
    impl_->initialized = false;
}

bool RmlFontEngine::LoadFontFace(const Rml::String& fileName, int faceIndex, bool fallbackFace,
                                 Rml::Style::FontWeight weight) {
    return LoadFontFace(fileName, faceIndex, kBundledLatoFamily, Rml::Style::FontStyle::Normal, weight, fallbackFace);
}

bool RmlFontEngine::LoadFontFace(const Rml::String& fileName, int faceIndex, const Rml::String& family,
                                 Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallbackFace) {
    if (!impl_->initialized || !impl_->library || faceIndex < 0 || !IsBundledFacePath(fileName))
        return false;

    const std::string path(fileName);
    for (const auto& loaded : impl_->faces) {
        if (loaded->path == path && loaded->family == family && loaded->style == style && loaded->weight == weight)
            return true;
    }

    auto loaded = std::make_unique<FontFace>();
    loaded->path = path;
    loaded->family = family;
    loaded->style = style;
    loaded->weight = weight;
    loaded->fallback = fallbackFace;
    if (FT_New_Face(impl_->library, path.c_str(), faceIndex, &loaded->face) != 0)
        return false;
    impl_->faces.push_back(std::move(loaded));
    return true;
}

bool RmlFontEngine::LoadFontFace(Rml::Span<const Rml::byte> data, int faceIndex, const Rml::String& family,
                                 Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallbackFace) {
    (void)data;
    (void)faceIndex;
    (void)family;
    (void)style;
    (void)weight;
    (void)fallbackFace;
    return false; // This lane accepts packaged files only; in-memory/system fonts are not font sources.
}

Rml::FontFaceHandle RmlFontEngine::GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle style,
                                                     Rml::Style::FontWeight weight, int size) {
    FontFace* face = impl_->FindFace(family, style, weight);
    return face ? reinterpret_cast<Rml::FontFaceHandle>(impl_->GetOrCreateHandle(face, size)) : Rml::FontFaceHandle{};
}

Rml::FontEffectsHandle RmlFontEngine::PrepareFontEffects(Rml::FontFaceHandle handle, const Rml::FontEffectList& effects) {
    (void)handle;
    (void)effects;
    return 0;
}

const Rml::FontMetrics& RmlFontEngine::GetFontMetrics(Rml::FontFaceHandle handle) {
    static const Rml::FontMetrics empty{};
    FontHandle* fontHandle = FromHandle(handle);
    return fontHandle ? fontHandle->metrics : empty;
}

int RmlFontEngine::GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string,
                                  const Rml::TextShapingContext& shapingContext, Rml::Character priorCharacter) {
    FontHandle* fontHandle = FromHandle(handle);
    if (fontHandle && impl_->preset && fontHandle->font && fontHandle->font->face &&
        SetFontSize(fontHandle->font->face, fontHandle->size)) {
        const ShapedText shaped = ShapeUtf8(*impl_->preset, impl_->requiredScriptTags,
                                            fontHandle->font->face,
                                            std::string_view(string.begin(), string.size()),
                                            impl_->language);
        if (!shaped.visualRuns.empty()) {
            float width = 0.0f;
            const float letterSpacing = shapingContext.letter_spacing;
            for (const ShapedRun& run : shaped.visualRuns) {
                for (std::size_t index = 0; index < run.glyphs.size(); ++index) {
                    const ShapedGlyph& glyph = run.glyphs[index];
                    width += static_cast<float>(glyph.xAdvance) / 64.0f;
                    if (index + 1 == run.glyphs.size() ||
                        run.glyphs[index + 1].cluster != glyph.cluster)
                        width += letterSpacing;
                }
            }
            return RoundWidth(width);
        }
    }
    return fontHandle ? RoundWidth(MeasureWidth(*fontHandle, string, shapingContext, priorCharacter)) : 0;
}

int RmlFontEngine::GenerateString(Rml::RenderManager& renderManager, Rml::FontFaceHandle faceHandle,
                                  Rml::FontEffectsHandle effectsHandle, Rml::StringView string,
                                  Rml::Vector2f position, Rml::ColourbPremultiplied colour, float opacity,
                                  const Rml::TextShapingContext& shapingContext, Rml::TexturedMeshList& meshList) {
    (void)effectsHandle;
    (void)opacity;
    FontHandle* handle = FromHandle(faceHandle);
    if (!handle || !handle->font || !handle->font->face)
        return 0;

    FT_Face face = handle->font->face;
    GlyphAtlas& atlas = handle->atlas;
    if (!SetFontSize(face, handle->size))
        return 0;

    if (impl_->preset) {
        const ShapedText shaped = ShapeUtf8(*impl_->preset, impl_->requiredScriptTags,
                                            face, std::string_view(string.begin(), string.size()),
                                            impl_->language);
        if (!shaped.visualRuns.empty()) {
            float penX = position.x;
            float penY = position.y;
            bool hasGeometry = false;
            Rml::TexturedMesh textMesh;

            for (const ShapedRun& run : shaped.visualRuns) {
                for (std::size_t glyphPosition = 0; glyphPosition < run.glyphs.size(); ++glyphPosition) {
                    const ShapedGlyph& shapedGlyph = run.glyphs[glyphPosition];
                    const FT_UInt glyphIndex = static_cast<FT_UInt>(shapedGlyph.glyphId);
                    const bool glyphAdded = glyphIndex == 0
                                                ? atlas.AddReplacementGlyph(face, handle->size)
                                                : atlas.AddGlyph(face, glyphIndex, handle->size);
                    if (glyphAdded) {
                        if (const AtlasGlyph* glyph = atlas.FindGlyph(glyphIndex);
                            glyph && glyph->width > 0.0f && glyph->height > 0.0f) {
                            hasGeometry = true;
                            const float padding = glyph->padding;
                            const float leftBearing = std::floor(glyph->bearingX + padding);
                            const float topBearing = std::ceil(glyph->bearingY - padding);
                            const float offsetX = static_cast<float>(shapedGlyph.xOffset) / 64.0f;
                            const float offsetY = static_cast<float>(shapedGlyph.yOffset) / 64.0f;
                            const float x0 = penX + offsetX + leftBearing - padding;
                            const float y0 = penY - offsetY - topBearing - padding;
                            const float x1 = x0 + glyph->width;
                            const float y1 = y0 + glyph->height;
                            const int firstVertex = static_cast<int>(textMesh.mesh.vertices.size());
                            textMesh.mesh.vertices.push_back({Rml::Vector2f(x0, y0), colour,
                                                              Rml::Vector2f(glyph->u0, glyph->v0)});
                            textMesh.mesh.vertices.push_back({Rml::Vector2f(x1, y0), colour,
                                                              Rml::Vector2f(glyph->u1, glyph->v0)});
                            textMesh.mesh.vertices.push_back({Rml::Vector2f(x1, y1), colour,
                                                              Rml::Vector2f(glyph->u1, glyph->v1)});
                            textMesh.mesh.vertices.push_back({Rml::Vector2f(x0, y1), colour,
                                                              Rml::Vector2f(glyph->u0, glyph->v1)});
                            const int indices[6] = {firstVertex, firstVertex + 1, firstVertex + 2,
                                                    firstVertex, firstVertex + 2, firstVertex + 3};
                            textMesh.mesh.indices.insert(textMesh.mesh.indices.end(),
                                                         std::begin(indices), std::end(indices));
                        }
                    }

                    penX += static_cast<float>(shapedGlyph.xAdvance) / 64.0f;
                    penY -= static_cast<float>(shapedGlyph.yAdvance) / 64.0f;
                    if (glyphPosition + 1 == run.glyphs.size() ||
                        run.glyphs[glyphPosition + 1].cluster != shapedGlyph.cluster)
                        penX += shapingContext.letter_spacing;
                }
            }

            if (hasGeometry) {
                textMesh.texture = atlas.GetTexture(renderManager);
                meshList.push_back(std::move(textMesh));
            }
            return RoundWidth(penX - position.x);
        }
    }

    const bool kerningEnabled = shapingContext.font_kerning != Rml::Style::FontKerning::None && FT_HAS_KERNING(face);
    float penX = position.x;
    FT_UInt previousGlyph = 0;
    bool hasGeometry = false;
    Rml::TexturedMesh textMesh;

    for (auto it = Rml::StringIteratorU8(string); it; ++it) {
        const FT_UInt glyphIndex = GlyphIndex(face, *it);
        if (kerningEnabled && previousGlyph != 0 && glyphIndex != 0) {
            FT_Vector kerning{};
            if (FT_Get_Kerning(face, previousGlyph, glyphIndex, FT_KERNING_DEFAULT, &kerning) == 0)
                penX += static_cast<float>(kerning.x >> 6);
        }

        float glyphAdvance = 0.0f;
        bool glyphAdded = false;
        if (glyphIndex == 0) {
            glyphAdvance = MissingGlyphAdvance(handle->size);
            glyphAdded = atlas.AddReplacementGlyph(face, handle->size);
        } else if (FT_Load_Glyph(face, glyphIndex, kSizedGlyphLoadFlags) == 0) {
            glyphAdvance = static_cast<float>(face->glyph->metrics.horiAdvance >> 6);
            glyphAdded = atlas.AddGlyph(face, glyphIndex, handle->size);
        }

        if (glyphAdded) {
            if (const AtlasGlyph* glyph = atlas.FindGlyph(glyphIndex); glyph && glyph->width > 0.0f && glyph->height > 0.0f) {
                hasGeometry = true;
                const float padding = glyph->padding;
                const float leftBearing = std::floor(glyph->bearingX + glyph->padding);
                const float topBearing = std::ceil(glyph->bearingY - glyph->padding);
                const float x0 = penX + leftBearing - padding;
                const float y0 = position.y - topBearing - padding;
                const float x1 = x0 + glyph->width;
                const float y1 = y0 + glyph->height;
                const int firstVertex = static_cast<int>(textMesh.mesh.vertices.size());
                textMesh.mesh.vertices.push_back({Rml::Vector2f(x0, y0), colour, Rml::Vector2f(glyph->u0, glyph->v0)});
                textMesh.mesh.vertices.push_back({Rml::Vector2f(x1, y0), colour, Rml::Vector2f(glyph->u1, glyph->v0)});
                textMesh.mesh.vertices.push_back({Rml::Vector2f(x1, y1), colour, Rml::Vector2f(glyph->u1, glyph->v1)});
                textMesh.mesh.vertices.push_back({Rml::Vector2f(x0, y1), colour, Rml::Vector2f(glyph->u0, glyph->v1)});
                const int indices[6] = {firstVertex, firstVertex + 1, firstVertex + 2,
                                        firstVertex, firstVertex + 2, firstVertex + 3};
                textMesh.mesh.indices.insert(textMesh.mesh.indices.end(), std::begin(indices), std::end(indices));
            }
        }
        penX += glyphAdvance;
        penX += shapingContext.letter_spacing;
        previousGlyph = glyphIndex;
    }

    if (hasGeometry) {
        textMesh.texture = atlas.GetTexture(renderManager);
        meshList.push_back(std::move(textMesh));
    }
    return RoundWidth(penX - position.x);
}

int RmlFontEngine::GetVersion(Rml::FontFaceHandle handle) {
    FontHandle* fontHandle = FromHandle(handle);
    return fontHandle && fontHandle->font ? static_cast<int>(fontHandle->atlas.Version()) : 0;
}

void RmlFontEngine::ReleaseFontResources() {
    impl_->handles.clear();
    impl_->handleLookup.clear();
}

} // namespace Vixen::Text
