#pragma once

#include <RmlUi/Core/FontEngineInterface.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace Vixen::Text {

struct PresetDesc;

// The ordinary RmlUi label path uses bundled font faces and the module's
// FreeType metrics/MSDF atlas. SetPresetDesc opts measurement and drawing into
// HarfBuzz shaping with SheenBidi run ordering; line breaking stays a separate
// module API. The adapter never enumerates operating-system fonts.
class RmlFontEngine final : public Rml::FontEngineInterface {
public:
    RmlFontEngine();
    ~RmlFontEngine() override;

    RmlFontEngine(const RmlFontEngine&) = delete;
    RmlFontEngine& operator=(const RmlFontEngine&) = delete;

    // Preset metadata is copied at this boundary, including the resolved
    // required-script tags, so callers may release their generated table view.
    // When no preset is active, the lane-7 legacy path remains unchanged.
    void SetPresetDesc(const PresetDesc& preset,
                       std::span<const std::uint32_t> requiredScriptTags,
                       std::string_view language = {});
    void ClearPresetDesc();

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
