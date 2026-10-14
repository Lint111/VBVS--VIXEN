#pragma once

#include <RmlUi/Core/FontEngineInterface.h>

#include <memory>

namespace Vixen::Text {

// The ordinary RmlUi label path uses the active bundled font faces and the
// module's FreeType metrics/MSDF atlas. This adapter intentionally does not
// enumerate operating-system fonts or provide shaping/bidi/line breaking.
class RmlFontEngine final : public Rml::FontEngineInterface {
public:
    RmlFontEngine();
    ~RmlFontEngine() override;

    RmlFontEngine(const RmlFontEngine&) = delete;
    RmlFontEngine& operator=(const RmlFontEngine&) = delete;

    void Initialize() override;
    void Shutdown() override;
    bool LoadFontFace(const Rml::String& fileName, int faceIndex, bool fallbackFace,
                      Rml::Style::FontWeight weight) override;
    bool LoadFontFace(const Rml::String& fileName, int faceIndex, const Rml::String& family,
                      Rml::Style::FontStyle style, Rml::Style::FontWeight weight,
                      bool fallbackFace) override;
    bool LoadFontFace(Rml::Span<const Rml::byte> data, int faceIndex, const Rml::String& family,
                      Rml::Style::FontStyle style, Rml::Style::FontWeight weight,
                      bool fallbackFace) override;
    Rml::FontFaceHandle GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle style,
                                         Rml::Style::FontWeight weight, int size) override;
    Rml::FontEffectsHandle PrepareFontEffects(Rml::FontFaceHandle handle,
                                              const Rml::FontEffectList& effects) override;
    const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override;
    int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string,
                       const Rml::TextShapingContext& shapingContext,
                       Rml::Character priorCharacter = Rml::Character::Null) override;
    int GenerateString(Rml::RenderManager& renderManager, Rml::FontFaceHandle faceHandle,
                       Rml::FontEffectsHandle effectsHandle, Rml::StringView string,
                       Rml::Vector2f position, Rml::ColourbPremultiplied colour, float opacity,
                       const Rml::TextShapingContext& shapingContext,
                       Rml::TexturedMeshList& meshList) override;
    int GetVersion(Rml::FontFaceHandle handle) override;
    void ReleaseFontResources() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Vixen::Text
