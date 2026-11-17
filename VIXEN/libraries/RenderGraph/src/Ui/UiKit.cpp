#include "Ui/UiKit.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/StreamMemory.h>
#include <RmlUi/Core/SystemInterface.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace Vixen::RenderGraph::UiKit {

// ======================================================================================================== number rule
bool ParseNumber(const std::string& text, float& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(v)) return false;
    out = static_cast<float>(v);
    return true;
}

std::string FormatNumber(float value) {
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.6g", static_cast<double>(value));
    return buffer;
}

float DefaultStep(bool hasRange, float minimum, float maximum) {
    const float span = maximum - minimum;
    return hasRange && span > 0.0f ? span / 8.0f : 0.25f;
}

float Nudge(float value, int direction, float step, float multiplier, bool hasRange, float minimum, float maximum) {
    float next = value + static_cast<float>(direction) * step * multiplier;
    if (hasRange) next = std::clamp(next, minimum, maximum);
    return next;
}

float ScrubValue(float start, float dxPixels, float step, bool hasRange, float minimum, float maximum) {
    const float span = hasRange ? maximum - minimum : 40.0f * step;
    float next = start + dxPixels * (span / kScrubPixelsPerRange);
    if (hasRange) next = std::clamp(next, minimum, maximum);
    return next;
}

std::string FormatRange(bool hasRange, float minimum, float maximum) {
    if (!hasRange) return {};
    return "range " + FormatNumber(minimum) + "\xE2\x80\x93" + FormatNumber(maximum);  // en dash
}

std::string ContrastingTextHex(std::uint32_t srgbColor) {
    constexpr std::uint32_t kLightText = 0xF0F2FFu;
    constexpr std::uint32_t kDarkText = 0x101826u;
    const auto linear = [](std::uint32_t channel) {
        const float srgb = static_cast<float>(channel) / 255.0f;
        return srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
    };
    const float luminance =
        0.2126f * linear((srgbColor >> 16u) & 0xFFu) +
        0.7152f * linear((srgbColor >> 8u) & 0xFFu) +
        0.0722f * linear(srgbColor & 0xFFu);
    const auto contrast = [luminance](std::uint32_t textColor) {
        const auto channel = [](std::uint32_t color, unsigned shift) {
            const float srgb = static_cast<float>((color >> shift) & 0xFFu) / 255.0f;
            return srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
        };
        const float textLuminance = 0.2126f * channel(textColor, 16u) +
                                    0.7152f * channel(textColor, 8u) +
                                    0.0722f * channel(textColor, 0u);
        const float brighter = std::max(luminance, textLuminance);
        const float darker = std::min(luminance, textLuminance);
        return (brighter + 0.05f) / (darker + 0.05f);
    };
    const auto hex = [](std::uint32_t color) {
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "#%06X", color & 0x00FFFFFFu);
        return std::string(buffer);
    };
    return hex(contrast(kLightText) >= contrast(kDarkText) ? kLightText : kDarkText);
}

std::string LayerOpShortLabel(std::string_view catalogueName) {
    // This table is the one presentation rule for the generated layer-op catalogue. The full
    // catalogue name remains the row title, so a compact label never hides which operation is
    // selected.
    if (catalogueName == "SmoothUnion") return "Smooth";
    if (catalogueName == "Subtract") return "Subtract";
    if (catalogueName == "Intersect") return "Intersect";
    if (catalogueName == "Union") return "Union";
    if (catalogueName == "Provide") return "Provide";
    return std::string(catalogueName);
}

// ================================================================================================ the override contract
namespace {

enum class Kind { Button, Disclosure, Toggle, TextField, NumberField, ChoiceGroup, List, Toolbar, StatusLine, Panel, Flyout };

struct KindInfo {
    Kind kind;
    const char* tag;
    const char* fragment;                 // assets/ui/kit/<fragment>.rml; null = styled by kit.rcss only
    std::vector<std::string> attributes;  // the override contract
    std::vector<std::string> parts;       // parts the fragment must provide
};

const std::vector<KindInfo>& Kinds() {
    static const std::vector<KindInfo> kinds = {
        {Kind::Button, "ut-button", "button", {"label", "variant", "disabled", "reason", "icon"}, {"icon", "label", "reason"}},
        {Kind::Disclosure, "ut-disclosure", "disclosure", {"label", "open", "disabled", "reason", "tabindex"},
         {"marker", "label", "reason"}},
        {Kind::Toggle, "ut-toggle", "toggle", {"checked", "variant"}, {"mark"}},
        {Kind::TextField, "ut-text-field", nullptr, {"value", "autofocus", "variant", "type"}, {}},
        {Kind::NumberField, "ut-number-field", "number-field",
         {"label", "value", "value-id", "reset-id", "default", "min", "max", "unit", "step", "variant"},
         {"label", "input", "down", "up", "reset", "range"}},
        {Kind::ChoiceGroup, "ut-choice-group", "choice-group", {"label", "variant"}, {"label"}},
        {Kind::List, "ut-list", nullptr, {"variant"}, {}},
        {Kind::Toolbar, "ut-toolbar", nullptr, {"variant"}, {}},
        {Kind::StatusLine, "ut-status-line", "status-line", {"doc", "state", "undo", "redo", "message", "variant"},
         {"doc", "state", "history", "message-sep", "message"}},
        {Kind::Panel, "ut-panel", nullptr, {"variant"}, {}},
        {Kind::Flyout, "ut-flyout", nullptr, {"variant"}, {}},
    };
    return kinds;
}

const KindInfo* Find(const std::string& tag) {
    for (const auto& k : Kinds()) if (tag == k.tag) return &k;
    return nullptr;
}

bool StartsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

}  // namespace

const std::vector<std::string>& Tags() {
    static const std::vector<std::string> tags = [] {
        std::vector<std::string> out;
        for (const auto& k : Kinds()) out.emplace_back(k.tag);
        return out;
    }();
    return tags;
}

const std::vector<std::string>& ContractAttributes(const std::string& tag) {
    static const std::vector<std::string> none;
    const KindInfo* info = Find(tag);
    return info ? info->attributes : none;
}

bool AttributeAllowed(const std::string& tag, const std::string& name) {
    const KindInfo* info = Find(tag);
    if (!info) return true;
    if (name == "id" || name == "class" || name == "style" || name == "title") return true;
    if (StartsWith(name, "data-attr-")) return AttributeAllowed(tag, name.substr(10));
    if (StartsWith(name, "data-")) return true;  // data-for / data-if / data-class-* / data-value: the document's own bindings
    return std::find(info->attributes.begin(), info->attributes.end(), name) != info->attributes.end();
}

std::vector<std::string> RefusedAttributes(const std::string& tag, const std::vector<std::string>& names) {
    std::vector<std::string> refused;
    for (const auto& n : names) if (!AttributeAllowed(tag, n)) refused.push_back(n);
    return refused;
}

// ================================================================================================== fragments + host
struct Registry::Impl {
    std::function<std::string(const std::string&)> resolve;
    struct Cached { std::filesystem::file_time_type mtime{}; std::string text; };
    std::map<std::string, Cached> cache;
    std::set<std::string> reported;       // one diagnostic per (document, tag, attribute)
    std::vector<std::unique_ptr<Rml::ElementInstancer>> instancers;
    bool registered = false;

    std::string FragmentPath(const char* fragment) const {
        return resolve(std::string("assets/ui/kit/") + fragment + ".rml");
    }

    // The fragment text. A miss in the cache (or a changed mtime) re-reads through RmlUi's file interface, so
    // a tracked document load records the file.
    bool Read(const std::string& path, std::string& text) {
        std::error_code ec;
        const auto mtime = std::filesystem::last_write_time(path, ec);
        if (!ec) {
            auto it = cache.find(path);
            if (it != cache.end() && it->second.mtime == mtime) { text = it->second.text; return true; }
        }
        Rml::FileInterface* files = Rml::GetFileInterface();
        if (!files) return false;
        const Rml::FileHandle handle = files->Open(path);
        if (!handle) return false;
        std::string data;
        char buffer[1024];
        for (size_t n; (n = files->Read(buffer, sizeof(buffer), handle)) > 0;) data.append(buffer, n);
        files->Close(handle);
        cache[path] = {mtime, data};
        text = std::move(data);
        return true;
    }

    void Report(const std::string& file, int line, const std::string& message) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "%s:%d: %s", file.c_str(), line, message.c_str());
    }

    // Refuses an instance attribute outside the contract, naming the document file and the line of the element.
    void CheckAttributes(Rml::Element* parent, const std::string& tag, const Rml::XMLAttributes& attributes) {
        std::vector<std::string> names;
        for (const auto& [name, value] : attributes) {
            // The kit stamps its own hit-mask on buttons/toggles (a data-for row clone copies it back): only a value the kit
            // itself would write passes; any other hit-mask is the instance trying to skip the hit rules.
            if (name == "hit-mask") {
                const Rml::String v = value.Get<Rml::String>();
                if (v == "none" || v == "ellipse") continue;
            }
            names.push_back(name);
        }
        const auto refused = RefusedAttributes(tag, names);
        if (refused.empty()) return;
        const Rml::ElementDocument* doc = parent ? parent->GetOwnerDocument() : nullptr;
        const std::string file = doc ? doc->GetSourceURL() : std::string("<document>");
        for (const auto& name : refused) {
            const std::string key = file + "|" + tag + "|" + name;
            if (!reported.insert(key).second) continue;
            int line = 0;
            if (doc) {
                std::ifstream in(file);
                std::string text;
                for (int n = 1; std::getline(in, text); ++n) {
                    if (text.find("<" + tag) != std::string::npos && text.find(name) != std::string::npos) { line = n; break; }
                }
            }
            std::string allowed;
            for (const auto& a : ContractAttributes(tag)) allowed += (allowed.empty() ? "" : ", ") + a;
            Report(file, line, "<" + tag + "> attribute '" + name + "' is not part of the kit override contract (allowed: " +
                                   allowed + ", plus id, class, style, title, data-*)");
        }
    }
};

namespace {

Rml::Element* FindPart(Rml::Element& root, const std::string& name) {
    for (int i = 0; i < root.GetNumChildren(true); ++i) {
        Rml::Element* child = root.GetChild(i);
        if (!child) continue;
        if (child->GetAttribute<Rml::String>("part", "") == name) return child;
        if (Rml::Element* deeper = FindPart(*child, name)) return deeper;
    }
    return nullptr;
}

void SetVisible(Rml::Element* element, bool visible) {
    if (!element) return;
    if (visible) element->RemoveProperty("display");
    else element->SetProperty("display", "none");
}

void SetText(Rml::Element* element, const std::string& text) {
    if (!element) return;
    if (element->GetAttribute<Rml::String>("data-kit-text", "\x01") == text) return;
    element->SetAttribute("data-kit-text", text);
    Rml::String escaped;
    for (char c : text) {
        switch (c) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            default: escaped += c;
        }
    }
    element->SetInnerRML(escaped);
}

bool Truthy(const std::string& v) { return v == "true" || v == "1" || v == "checked" || v == "yes"; }

constexpr float kShiftStepMultiplier = 10.0f;

class KitElement final : public Rml::Element, public Rml::EventListener {
public:
    KitElement(const Rml::String& tag, const KindInfo& info, Registry::Impl& host)
        : Rml::Element(tag), info_(info), host_(host) {}
    ~KitElement() override { EndScrub(); }

    // Instances the fragment into this element (once, at creation) and finds its parts.
    void Build() {
        if (info_.fragment) {
            const std::string path = host_.FragmentPath(info_.fragment);
            std::string text;
            if (!host_.Read(path, text)) {
                host_.Report(path, 0, "kit fragment for <" + std::string(info_.tag) + "> cannot be read");
            } else {
                // Wrapped in the document base tag on the SAME line, so error lines stay the fragment's own.
                const std::string wrapped = "<body>" + text + "</body>";
                Rml::StreamMemory stream(reinterpret_cast<const Rml::byte*>(wrapped.data()), wrapped.size());
                stream.SetSourceURL(path);
                Rml::Factory::InstanceElementStream(this, &stream);
                fragmentChildren_ = GetNumChildren(true);
                for (const auto& part : info_.parts) {
                    if (!FindPart(*this, part))
                        host_.Report(path, 1, "kit fragment is missing the part '" + part + "' that <" + info_.tag + "> needs");
                }
            }
        }
        if (info_.kind == Kind::Button || info_.kind == Kind::Disclosure || info_.kind == Kind::Toggle)
            SetAttribute("hit-mask", "none");
        if (info_.kind == Kind::NumberField) {
            if (auto* input = Part("input")) input->AddEventListener("change", this);
            if (auto* up = Part("up")) up->AddEventListener("click", this);
            if (auto* down = Part("down")) down->AddEventListener("click", this);
            if (auto* label = Part("label")) label->AddEventListener("mousedown", this);
        }
        if (info_.kind == Kind::Disclosure) AddEventListener("keydown", this);
        built_ = true;
        Refresh();
    }

    // A data-for row clone (and any caller) replaces the instance's own contents with SetInnerRML; the kit's structure
    // is not the instance's content, so only what follows the fragment's children is replaced.
    void SetInnerRML(const Rml::String& rml) override {
        while (GetNumChildren(true) > fragmentChildren_) RemoveChild(GetLastChild());
        if (!rml.empty()) Rml::Factory::InstanceElementText(this, rml);
    }

    void OnAttributeChange(const Rml::ElementAttributes& changed) override {
        Rml::Element::OnAttributeChange(changed);
        if (!built_) return;
        for (const auto& [name, value] : changed) {
            (void)value;
            if (name == "variant") ApplyVariant();
        }
        Refresh();
    }

    void ProcessEvent(Rml::Event& event) override {
        const Rml::String& type = event.GetType();
        if (info_.kind == Kind::Disclosure) {
            const int key = event.GetParameter<int>("key_identifier", 0);
            if (type == "keydown" && (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER)) {
                DispatchEvent("click", {});
                event.StopPropagation();
            }
            return;
        }
        if (info_.kind != Kind::NumberField) return;
        if (type == "change" && event.GetTargetElement() == Part("input")) {
            // Mirror what is typed into our own value attribute: after a refused commit the data binding then sees
            // a difference from the model value and writes the model value back (the field snaps back).
            if (!mirroring_) {
                mirroring_ = true;
                SetAttribute("value", Part("input")->GetAttribute<Rml::String>("value", ""));
                mirroring_ = false;
            }
        } else if (type == "click") {
            Nudge(event.GetTargetElement() == Part("up") ? 1 : -1, event);
        } else if (type == "mousedown" && event.GetParameter<int>("button", 0) == 0) {
            BeginScrub(event);
        } else if (type == "mousemove" && scrubbing_) {
            MoveScrub(event);
        } else if (type == "mouseup" && scrubbing_) {
            EndScrubCommit();
        }
    }

private:
    Rml::Element* Part(const char* name) { return FindPart(*this, name); }
    std::string Attr(const char* name) const { return GetAttribute<Rml::String>(name, ""); }

    void ApplyVariant() {
        const std::string variant = Attr("variant");
        const auto each = [](const std::string& list, const std::function<void(const std::string&)>& fn) {
            std::istringstream words(list);
            for (std::string w; words >> w;) fn(w);
        };
        each(appliedVariant_, [this](const std::string& w) { SetClass(w, false); });
        each(variant, [this](const std::string& w) { SetClass(w, true); });  // "tool close": every word is a class
        appliedVariant_ = variant;
        if (info_.kind == Kind::Button) {
            bool round = false;
            each(variant, [&](const std::string& w) { round = round || w == "round"; });
            SetAttribute("hit-mask", round ? "ellipse" : "none");
        }
    }

    void RefreshIcon() {
        if (info_.kind != Kind::Button) return;
        const std::string icon = Attr("icon");
        for (const char* name : {"provide", "solo", "up", "down", "remove"})
            SetClass(std::string("icon-") + name, icon == name);
    }

    // Do not use the Windows SDK's min/max macro names in the kit's native state.
    // The identifier guard runs before a Windows build and intentionally catches these
    // declarations even though Linux preprocessors do not expand them.
    struct Range { bool has = false; float minimum = 0.0f; float maximum = 0.0f; };
    Range ReadRange() const {
        Range r;
        r.has = ParseNumber(Attr("min"), r.minimum) && ParseNumber(Attr("max"), r.maximum) && r.maximum >= r.minimum;
        return r;
    }

    void Refresh() {
        switch (info_.kind) {
            case Kind::Button:
                RefreshIcon();
                SetText(Part("label"), Attr("label"));
                RefreshActionState();
                break;
            case Kind::Disclosure:
            {
                SetText(Part("label"), Attr("label"));
                const bool open = Truthy(Attr("open"));
                SetText(Part("marker"), open ? "v" : ">");
                SetClass("open", open);
                RefreshActionState();
                break;
            }
            case Kind::ChoiceGroup: SetText(Part("label"), Attr("label")); break;
            case Kind::Toggle: SetClass("checked", Truthy(Attr("checked"))); break;
            case Kind::StatusLine: RefreshStatus(); break;
            case Kind::NumberField: RefreshNumber(); break;
            default: break;
        }
    }

    void RefreshActionState() {
        const bool disabled = Truthy(Attr("disabled"));
        const std::string reason = Attr("reason");
        SetClass("disabled", disabled);
        // A disabled action with a reason remains hit-testable so the user can discover why it
        // cannot be removed. The editor's action dispatcher still owns the refusal (and therefore
        // cannot mutate the model); the title and editor status line carry the explanation.
        SetProperty("pointer-events", disabled && reason.empty() ? "none" : "auto");
        SetText(Part("reason"), reason);
        // Visibility is deliberately CSS-controlled. Always making the reason visible here turns
        // every node row into a repeated diagnostic column; the editor keeps this part hidden and
        // exposes the explanation through the title/status line after a refused action.
        if (disabled && !reason.empty() && GetAttribute<Rml::String>("title", "") != reason)
            SetAttribute("title", reason);
    }

    void RefreshStatus() {
        SetText(Part("doc"), Attr("doc"));
        SetText(Part("state"), Attr("state"));
        SetText(Part("history"), "undo " + Attr("undo") + " / redo " + Attr("redo"));
        const bool hasMessage = !Attr("message").empty();
        SetVisible(Part("message-sep"), hasMessage);
        SetVisible(Part("message"), hasMessage);
        SetText(Part("message"), Attr("message"));
    }

    void RefreshNumber() {
        const std::string unit = Attr("unit");
        SetText(Part("label"), Attr("label") + (unit.empty() ? "" : " (" + unit + ")"));
        if (auto* input = Part("input")) {
            if (input->GetId() != Attr("value-id")) input->SetId(Attr("value-id"));
            if (!mirroring_ && input->GetAttribute<Rml::String>("value", "") != Attr("value"))
                input->SetAttribute("value", Attr("value"));
        }
        const Range range = ReadRange();
        auto* rangePart = Part("range");
        SetText(rangePart, FormatRange(range.has, range.minimum, range.maximum));
        SetVisible(rangePart, range.has);
        auto* reset = Part("reset");
        const std::string resetId = Attr("reset-id");
        float current = 0.0f, def = 0.0f;
        const bool hasDefault = ParseNumber(Attr("default"), def);
        const bool atDefault = hasDefault && ParseNumber(Attr("value"), current) && current == def;
        if (reset) {
            if (reset->GetId() != resetId) reset->SetId(resetId);
            SetVisible(reset, !resetId.empty() && hasDefault && !atDefault);
            SetText(reset, "reset");
            if (hasDefault) reset->SetAttribute("title", "reset to " + FormatNumber(def));
        }
    }

    // The one commit path: the forwarder queues it like a typed value; the host runs one DispatchBySelector.
    void Commit(const std::string& text) {
        const std::string id = Attr("value-id");
        if (id.empty()) return;
        Rml::Dictionary parameters;
        parameters["id"] = id;
        parameters["text"] = text;
        DispatchEvent("ut-commit", parameters);
    }

    float CurrentValue() const {
        float v = 0.0f;
        ParseNumber(GetAttribute<Rml::String>("value", ""), v);
        return v;
    }
    float StepValue(const Range& r) const {
        float s = 0.0f;
        return ParseNumber(Attr("step"), s) && s > 0.0f ? s : DefaultStep(r.has, r.minimum, r.maximum);
    }

    // CAPABILITY (added after the migration, one place): Shift+nudge steps ten times as far, on every number field.
    static float StepMultiplier(Rml::Event& event) { return event.GetParameter<bool>("shift_key", false) ? kShiftStepMultiplier : 1.0f; }

    void Nudge(int direction, Rml::Event& event) {
        const Range r = ReadRange();
        const float before = CurrentValue();
        const float after = UiKit::Nudge(before, direction, StepValue(r), StepMultiplier(event), r.has, r.minimum, r.maximum);
        if (after != before) Commit(FormatNumber(after));
    }

    void BeginScrub(Rml::Event& event) {
        Rml::Context* ctx = GetContext();
        if (!ctx || scrubbing_) return;
        scrubbing_ = true;
        scrubMoved_ = false;
        scrubContext_ = ctx;
        scrubStartX_ = event.GetParameter<float>("mouse_x", 0.0f);
        scrubStartValue_ = CurrentValue();
        ctx->AddEventListener("mousemove", this, true);
        ctx->AddEventListener("mouseup", this, true);
    }
    void MoveScrub(Rml::Event& event) {
        const Range r = ReadRange();
        const float dx = event.GetParameter<float>("mouse_x", scrubStartX_) - scrubStartX_;
        scrubValue_ = ScrubValue(scrubStartValue_, dx, StepValue(r), r.has, r.minimum, r.maximum);
        scrubMoved_ = true;
        if (auto* input = Part("input")) {  // preview only; one commit on release is one undo entry
            mirroring_ = true;
            input->SetAttribute("value", FormatNumber(scrubValue_));
            mirroring_ = false;
        }
    }
    void EndScrub() {
        if (scrubbing_ && scrubContext_) {
            scrubContext_->RemoveEventListener("mousemove", this, true);
            scrubContext_->RemoveEventListener("mouseup", this, true);
        }
        scrubbing_ = false;
        scrubContext_ = nullptr;
    }
    void EndScrubCommit() {
        const bool moved = scrubMoved_;
        const float value = scrubValue_;
        EndScrub();
        if (moved && value != scrubStartValue_) Commit(FormatNumber(value));
        else if (auto* input = Part("input")) input->SetAttribute("value", Attr("value"));
    }

    const KindInfo& info_;
    Registry::Impl& host_;
    bool built_ = false;
    int fragmentChildren_ = 0;
    std::string appliedVariant_;
    bool mirroring_ = false;
    bool scrubbing_ = false;
    bool scrubMoved_ = false;
    Rml::Context* scrubContext_ = nullptr;
    float scrubStartX_ = 0.0f;
    float scrubStartValue_ = 0.0f;
    float scrubValue_ = 0.0f;
};

// <ut-text-field>: a real form control (data-value binds two-way), styled by the one kit rule.
class KitTextField final : public Rml::ElementFormControlInput {
public:
    explicit KitTextField(const Rml::String& tag) : Rml::ElementFormControlInput(tag) {}
};

class Instancer final : public Rml::ElementInstancer {
public:
    Instancer(const KindInfo& info, Registry::Impl& host) : info_(info), host_(host) {}
    Rml::ElementPtr InstanceElement(Rml::Element* parent, const Rml::String& tag, const Rml::XMLAttributes& attributes) override {
        host_.CheckAttributes(parent, tag, attributes);
        if (info_.kind == Kind::TextField) return Rml::ElementPtr(new KitTextField(tag));
        auto* element = new KitElement(tag, info_, host_);
        element->Build();
        return Rml::ElementPtr(element);
    }
    void ReleaseElement(Rml::Element* element) override { delete element; }
private:
    const KindInfo& info_;
    Registry::Impl& host_;
};

}  // namespace

Registry::Registry(std::function<std::string(const std::string&)> resolve) : impl_(std::make_unique<Impl>()) {
    impl_->resolve = std::move(resolve);
    for (const auto& k : Kinds()) impl_->instancers.push_back(std::make_unique<Instancer>(k, *impl_));
}

Registry::~Registry() { Unregister(); }

bool Registry::Register() {
    if (impl_->registered) return true;
    size_t i = 0;
    for (const auto& k : Kinds()) Rml::Factory::RegisterElementInstancer(k.tag, impl_->instancers[i++].get());
    impl_->registered = true;
    return true;
}

void Registry::Unregister() {
    if (!impl_ || !impl_->registered) return;
    for (const auto& k : Kinds()) Rml::Factory::RegisterElementInstancer(k.tag, nullptr);
    impl_->registered = false;
}

void Registry::TouchFragments() {
    for (const auto& k : Kinds()) {
        if (!k.fragment) continue;
        std::string text;
        impl_->cache.erase(impl_->FragmentPath(k.fragment));  // force the open through the (tracking) file interface
        impl_->Read(impl_->FragmentPath(k.fragment), text);
    }
}

}  // namespace Vixen::RenderGraph::UiKit
