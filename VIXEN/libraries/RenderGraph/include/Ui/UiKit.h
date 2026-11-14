#pragma once
// R874: the foundation UI kit. Base elements (<ut-button>, <ut-toggle>, <ut-text-field>, <ut-number-field>,
// <ut-choice-group>, <ut-list>, <ut-toolbar>, <ut-status-line>, <ut-panel>, <ut-flyout>) are registered ONCE here, in
// the generic UI node every app hosts (the <ut-text> precedent): nothing in them names the editor. Each element's
// behaviour lives in its one class (UiKit.cpp); its structure is a kit fragment file (assets/ui/kit/<name>.rml) and its
// look is assets/ui/kit/kit.rcss, so editing either on a running app updates every instance (the fragments are
// dependencies of every document: UiDocumentReloader::SetLoadPrelude + Registry::TouchFragments).
//
// Instances carry only the override contract of the design (label, bound field, selector slots, range, unit, step,
// variant, visibility via data-if); any other attribute is refused with a located diagnostic at document load.
// Value changes of every number field (typed, nudged, scrubbed) reach the host the same way: one `ut-commit` event
// that UiInputForwarder queues like a typed commit, so the host runs one DispatchBySelector.
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Vixen::RenderGraph::UiKit {

// ---- The one number rule (pure; unit-tested) ------------------------------------------------------------------
/// Parses a finite float. False for empty, non-numeric or non-finite text.
bool ParseNumber(const std::string& text, float& out);
/// Shortest round-trip-ish text of a value ("0.25", "3", "-1.5").
std::string FormatNumber(float value);
/// The nudge step when an instance gives none: an eighth of a finite range, else 0.25.
float DefaultStep(bool hasRange, float minimum, float maximum);
/// value + direction*step*multiplier, clamped to the range when there is one. direction is -1 or +1.
float Nudge(float value, int direction, float step, float multiplier, bool hasRange, float minimum, float maximum);
/// The scrubbed value after a horizontal drag of `dxPixels`: one full range per kScrubPixelsPerRange, or 40 steps when unbounded.
inline constexpr float kScrubPixelsPerRange = 200.0f;
float ScrubValue(float start, float dxPixels, float step, bool hasRange, float minimum, float maximum);
/// The ONE range text: "range 0–1" (empty when there is no range).
std::string FormatRange(bool hasRange, float minimum, float maximum);

// ---- The override contract --------------------------------------------------------------------------------------
/// Element names the kit registers.
const std::vector<std::string>& Tags();
/// Attributes an instance of `tag` may carry beyond the standard ones (id, class, style, title, data-*).
const std::vector<std::string>& ContractAttributes(const std::string& tag);
/// True when an instance of `tag` may carry `name` (a data-attr-<x> binding is judged by <x>).
bool AttributeAllowed(const std::string& tag, const std::string& name);
/// The attribute names in `names` that `tag` refuses, in order.
std::vector<std::string> RefusedAttributes(const std::string& tag, const std::vector<std::string>& names);

/// Registers every kit element with RmlUi once; the fragments are read through RmlUi's file interface.
class Registry {
public:
    /// `resolve` maps "assets/ui/kit/<name>.rml" to the file to read (the node's asset resolver).
    explicit Registry(std::function<std::string(const std::string&)> resolve);
    ~Registry();
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    bool Register();
    void Unregister();
    /// Opens every existing fragment through the file interface so a document load that is being tracked
    /// records them as its dependencies, whether or not the document instances that element yet.
    void TouchFragments();

    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace Vixen::RenderGraph::UiKit
