#pragma once

#include "Text/Abi.h"

#include <RmlUi/Core/Element.h>

#include <cstdint>
#include <memory>

namespace Rml {
class ElementInstancer;
}

namespace Vixen::Text {

enum class BuiltInTextTag : std::uint8_t {
    None,
    Emphasis,
    Color,
    Pause,
    Size,
};

// The loader owns the mapping from generated tag ordinals to the closed built-in set. Authored
// and profile/override tags can be reported through the same seam without requiring this renderer
// increment to invent their effects.
struct TextTagDescription {
    BuiltInTextTag builtIn = BuiltInTextTag::None;
    std::uint32_t rgba = 0xffffffffu;
    float sizeScale = 1.0f;
};

struct ResolvedText {
    TextArena arena{};
    TextElementSet elements{};
    TextPlacement placement{};
};

class TextResolver {
public:
    virtual ~TextResolver() = default;

    virtual bool Resolve(TextRef ref, ResolvedText& out) const = 0;
    virtual bool DescribeTag(const TextElement& element, TextTagDescription& out) const = 0;
};

/**
 * RmlUi custom element for a baked TextRef.
 *
 * The element never parses authored markup. The resolver supplies the already-baked plain row and
 * ordinal spans; this class turns those spans into ordinary RmlUi inline elements, so they use the
 * existing RmlFontEngine path. Rebuilding clears and recreates its children from the same immutable
 * source, making reflow, replay, and hot reload repeat-safe.
 */
class RmlTextElement final : public Rml::Element {
public:
    explicit RmlTextElement(const Rml::String& tag);
    ~RmlTextElement() override = default;

    void SetResolver(std::shared_ptr<const TextResolver> resolver);
    void SetTextRef(TextRef ref);
    void ClearTextRef();

    [[nodiscard]] TextRef GetTextRef() const noexcept { return ref_; }
    [[nodiscard]] bool HasTextRef() const noexcept { return hasRef_; }

    // Readable mode is the default: no animation is started by this element. The flags are kept on
    // the seam so a later authored-tag/profile implementation can opt in without changing layout.
    void SetMotionEnabled(bool enabled);
    void SetReducedMotion(bool reduced);
    [[nodiscard]] bool MotionEnabled() const noexcept { return motionEnabled_; }
    [[nodiscard]] bool ReducedMotion() const noexcept { return reducedMotion_; }

    // Replays the same TextRef into fresh children. Returns false when the resolver cannot resolve
    // the ref; the element is left empty rather than retaining stale dialogue.
    bool Rebuild();
    [[nodiscard]] std::uint32_t RebuildCount() const noexcept { return rebuildCount_; }

private:
    std::shared_ptr<const TextResolver> resolver_;
    TextRef ref_{};
    bool hasRef_ = false;
    bool motionEnabled_ = false;
    bool reducedMotion_ = true;
    std::uint32_t rebuildCount_ = 0;
};

/** Owns the non-owning RmlUi factory registration for <ut-text>. */
class RmlTextElementRegistry final {
public:
    explicit RmlTextElementRegistry(std::shared_ptr<const TextResolver> resolver = {});
    ~RmlTextElementRegistry();

    RmlTextElementRegistry(const RmlTextElementRegistry&) = delete;
    RmlTextElementRegistry& operator=(const RmlTextElementRegistry&) = delete;

    bool Register();
    void Unregister();
    void SetResolver(std::shared_ptr<const TextResolver> resolver);

    [[nodiscard]] bool IsRegistered() const noexcept { return registered_; }

private:
    class Instancer;
    std::unique_ptr<Instancer> instancer_;
    bool registered_ = false;
};

} // namespace Vixen::Text
