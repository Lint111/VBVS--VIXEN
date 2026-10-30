#include "Text/RmlTextElement.h"

#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Factory.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace Vixen::Text {

namespace {

constexpr const char* kElementName = "ut-text";

struct PlacementSpan {
    std::uint32_t start;
    std::uint32_t end;
    std::uint16_t element;
};

void AppendAttribute(Rml::Element* element, const char* name, std::uint32_t value) {
    element->SetAttribute(name, std::to_string(value));
}

void ApplyTagStyle(Rml::Element* element, const TextElement& source, const TextTagDescription& description) {
    switch (description.builtIn) {
    case BuiltInTextTag::Emphasis:
        element->SetClass("ut-text-emphasis", true);
        element->SetProperty("font-weight", "700");
        break;
    case BuiltInTextTag::Color: {
        element->SetClass("ut-text-color", true);
        const std::uint32_t red = description.rgba & 0xffu;
        const std::uint32_t green = (description.rgba >> 8u) & 0xffu;
        const std::uint32_t blue = (description.rgba >> 16u) & 0xffu;
        char color[8]{};
        std::snprintf(color, sizeof(color), "#%02x%02x%02x", red, green, blue);
        element->SetProperty("color", color);
        AppendAttribute(element, "data-ut-text-color", description.rgba);
        break;
    }
    case BuiltInTextTag::Pause:
        element->SetClass("ut-text-pause", true);
        element->SetAttribute("data-ut-text-pause", "true");
        break;
    case BuiltInTextTag::Size: {
        element->SetClass("ut-text-size", true);
        const float scale = description.sizeScale > 0.0f ? description.sizeScale : 1.0f;
        element->SetProperty("font-size", std::to_string(scale * 100.0f) + "%");
        break;
    }
    case BuiltInTextTag::None:
        break;
    }

    if (source.kind == TextElementKind::AuthoredTag) {
        element->SetClass("ut-text-authored", true);
        AppendAttribute(element, "data-ut-authored-tag", source.typeId);
    } else if (source.kind == TextElementKind::Hook) {
        // The authored-tag seam is intentionally observable but inert in this increment. A later
        // R784.6 handler can attach its event/profile behavior without changing the span layout.
        element->SetClass("ut-text-hook", true);
        AppendAttribute(element, "data-ut-text-hook", source.typeId);
    } else if (source.kind == TextElementKind::Placeholder) {
        element->SetClass("ut-text-placeholder", true);
        AppendAttribute(element, "data-ut-text-placeholder", source.typeId);
    }
}

} // namespace

RmlTextElement::RmlTextElement(const Rml::String& tag) : Rml::Element(tag) {
    SetAttribute("data-ut-text-motion", "reduced");
}

void RmlTextElement::SetResolver(std::shared_ptr<const TextResolver> resolver) {
    if (resolver_ == resolver)
        return;
    resolver_ = std::move(resolver);
    if (hasRef_)
        Rebuild();
}

void RmlTextElement::SetTextRef(TextRef ref) {
    if (hasRef_ && ref_.key == ref.key && ref_.structure == ref.structure)
        return;
    ref_ = ref;
    hasRef_ = true;
    Rebuild();
}

void RmlTextElement::ClearTextRef() {
    if (!hasRef_)
        return;
    hasRef_ = false;
    ref_ = {};
    Rebuild();
}

void RmlTextElement::SetMotionEnabled(bool enabled) {
    if (motionEnabled_ == enabled)
        return;
    motionEnabled_ = enabled;
    SetAttribute("data-ut-text-motion", motionEnabled_ && !reducedMotion_ ? "enabled" : "reduced");
}

void RmlTextElement::SetReducedMotion(bool reduced) {
    if (reducedMotion_ == reduced)
        return;
    reducedMotion_ = reduced;
    SetAttribute("data-ut-text-motion", motionEnabled_ && !reducedMotion_ ? "enabled" : "reduced");
}

bool RmlTextElement::Rebuild() {
    while (Element* child = GetFirstChild())
        RemoveChild(child);
    ++rebuildCount_;

    SetAttribute("data-ut-text-motion", motionEnabled_ && !reducedMotion_ ? "enabled" : "reduced");
    if (!hasRef_ || !resolver_ || !GetOwnerDocument())
        return false;

    ResolvedText resolved{};
    if (!resolver_->Resolve(ref_, resolved) || !resolved.arena.utf8Bytes || !resolved.arena.rowOffsets ||
        ref_.key >= resolved.arena.rowCount || (resolved.elements.count != 0u && !resolved.elements.elements) ||
        (resolved.placement.count != 0u && !resolved.placement.spans))
        return false;

    const std::uint32_t rowStart = resolved.arena.rowOffsets[ref_.key];
    const std::uint32_t rowEnd = resolved.arena.rowOffsets[ref_.key + 1u];
    if (rowStart > rowEnd || rowEnd > resolved.arena.byteCount)
        return false;

    const std::uint32_t rowLength = rowEnd - rowStart;
    std::vector<PlacementSpan> spans;
    spans.reserve(resolved.placement.count);
    std::vector<std::uint32_t> boundaries{0u, rowLength};
    for (std::uint16_t i = 0; i < resolved.placement.count; ++i) {
        const TextSpan& span = resolved.placement.spans[i];
        if (span.element >= resolved.elements.count)
            continue;
        const std::uint32_t start = std::min(span.byteStart, rowLength);
        const std::uint32_t end = std::min(std::max(span.byteEnd, start), rowLength);
        spans.push_back({start, end, span.element});
        if (start != end) {
            boundaries.push_back(start);
            boundaries.push_back(end);
        } else {
            boundaries.push_back(start);
        }
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

    auto makeSpan = [&](const TextElement& source) {
        Rml::ElementPtr node = GetOwnerDocument()->CreateElement("span");
        Rml::Element* span = node.get();
        TextTagDescription description{};
        const bool described = resolver_->DescribeTag(source, description);
        if (source.kind == TextElementKind::AuthoredTag || source.kind == TextElementKind::Hook ||
            source.kind == TextElementKind::Placeholder) {
            if (source.kind == TextElementKind::AuthoredTag) {
                span->SetClass("ut-text-authored", true);
                AppendAttribute(span, "data-ut-authored-tag", source.typeId);
            } else if (source.kind == TextElementKind::Hook) {
                span->SetClass("ut-text-hook", true);
                AppendAttribute(span, "data-ut-text-hook", source.typeId);
            } else {
                span->SetClass("ut-text-placeholder", true);
                AppendAttribute(span, "data-ut-text-placeholder", source.typeId);
            }
        }
        if (described)
            ApplyTagStyle(span, source, description);
        return node;
    };

    auto appendText = [&](Rml::Element* parent, std::uint32_t start, std::uint32_t end) {
        if (start >= end)
            return;
        parent->AppendChild(GetOwnerDocument()->CreateTextNode(
            Rml::String(resolved.arena.utf8Bytes + rowStart + start, end - start)));
    };

    auto appendMarkers = [&](std::uint32_t position) {
        for (const PlacementSpan& span : spans) {
            if (span.start != position || span.end != position)
                continue;
            Rml::ElementPtr node = makeSpan(resolved.elements.elements[span.element]);
            Rml::Element* marker = AppendChild(std::move(node));
            marker->SetProperty("display", "none");
        }
    };

    appendMarkers(0u);
    for (std::size_t boundary = 1; boundary < boundaries.size(); ++boundary) {
        const std::uint32_t start = boundaries[boundary - 1];
        const std::uint32_t end = boundaries[boundary];
        if (start == end)
            continue;

        std::vector<PlacementSpan> active;
        for (const PlacementSpan& span : spans) {
            if (span.start <= start && span.end >= end && span.start < span.end)
                active.push_back(span);
        }
        std::sort(active.begin(), active.end(), [](const PlacementSpan& lhs, const PlacementSpan& rhs) {
            if (lhs.start != rhs.start) return lhs.start < rhs.start;
            if (lhs.end != rhs.end) return lhs.end > rhs.end;
            return lhs.element < rhs.element;
        });

        Rml::Element* parent = this;
        std::vector<Rml::Element*> wrappers;
        wrappers.reserve(active.size());
        for (const PlacementSpan& span : active) {
            Rml::ElementPtr node = makeSpan(resolved.elements.elements[span.element]);
            parent = parent->AppendChild(std::move(node));
            wrappers.push_back(parent);
        }
        appendText(parent, start, end);
        appendMarkers(end);
    }
    if (boundaries.size() == 1)
        appendText(this, 0u, rowLength);
    return true;
}

class RmlTextElementRegistry::Instancer final : public Rml::ElementInstancer {
public:
    explicit Instancer(std::shared_ptr<const TextResolver> resolver) : resolver_(std::move(resolver)) {}

    Rml::ElementPtr InstanceElement(Rml::Element*, const Rml::String& tag,
                                    const Rml::XMLAttributes&) override {
        auto element = Rml::ElementPtr(new RmlTextElement(tag));
        static_cast<RmlTextElement*>(element.get())->SetResolver(resolver_);
        return element;
    }

    void ReleaseElement(Rml::Element* element) override { delete element; }

    void SetResolver(std::shared_ptr<const TextResolver> resolver) { resolver_ = std::move(resolver); }

private:
    std::shared_ptr<const TextResolver> resolver_;
};

RmlTextElementRegistry::RmlTextElementRegistry(std::shared_ptr<const TextResolver> resolver)
    : instancer_(std::make_unique<Instancer>(std::move(resolver))) {}

RmlTextElementRegistry::~RmlTextElementRegistry() { Unregister(); }

bool RmlTextElementRegistry::Register() {
    if (registered_)
        return true;
    Rml::Factory::RegisterElementInstancer(kElementName, instancer_.get());
    registered_ = true;
    return true;
}

void RmlTextElementRegistry::Unregister() {
    if (!registered_)
        return;
    Rml::Factory::RegisterElementInstancer(kElementName, nullptr);
    registered_ = false;
}

void RmlTextElementRegistry::SetResolver(std::shared_ptr<const TextResolver> resolver) {
    instancer_->SetResolver(std::move(resolver));
}

} // namespace Vixen::Text
