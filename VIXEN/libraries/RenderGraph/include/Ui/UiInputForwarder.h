#pragma once
// Generic keyboard / text / pointer forwarding from the app's InputState into an Rml::Context
// (R764: grow the generic UI path, no editor-only input layer). UIRenderNode owns one; every app that
// hosts RmlUi gets it, and with no focusable element it changes nothing a document can observe except
// :hover / pointer state.
//
// Typed values reach actions through the EXISTING element-trigger selectors (BindingStore): a text
// control's element id is a selector pattern whose last placeholder is the value slot, written as
// `{...}` ("parameter-0-set-{value}", "layer-2-name-{name}"). When the user commits the field (Enter,
// or blur after an edit) the forwarder queues Commit{selector, text} where `selector` is the id with
// the slot replaced by the typed text. The host drains the commits and calls DispatchBySelector, the
// same path a click takes: one ActionStack entry, refusals through the normal diagnostic path. No new
// declaration form: the trigger is an ordinary FlowElementTrigger, and the document carries the id.
#include "Data/InputState.h"

#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Input.h>

#include <functional>
#include <string>
#include <vector>

namespace Rml { class Context; class Element; class Event; }

namespace Vixen::RenderGraph {

/// GLFW key code -> RmlUi key identifier (KI_UNKNOWN for a key RmlUi has no name for).
Rml::Input::KeyIdentifier RmlKeyFromGlfw(int glfwKey);
/// GLFW modifier bitmask -> RmlUi KeyModifier mask.
int RmlModifiersFromGlfw(int glfwMods);

/// True when `elementId` ends in a value slot ("parameter-0-set-{value}"): the id is a pattern for a typed
/// value, not a selector, so a click on the control must not be dispatched as one (the host skips it; the
/// control takes focus instead).
bool HasValueSlot(const std::string& elementId);

/// The selector a committed text control dispatches: `elementId` with its last `{...}` slot replaced
/// by `text`. An id with no slot is returned unchanged (nothing to fill).
std::string SelectorFromCommittedId(const std::string& elementId, const std::string& text);

class UiInputForwarder final : public Rml::EventListener {
public:
    struct Commit {
        std::string elementId;  ///< the control's id (the selector pattern with its value slot)
        std::string selector;   ///< elementId with the slot filled by the committed text
        std::string text;
    };

    UiInputForwarder() = default;
    ~UiInputForwarder() override { Detach(); }
    UiInputForwarder(const UiInputForwarder&) = delete;
    UiInputForwarder& operator=(const UiInputForwarder&) = delete;

    /// Listen for focus / blur / change on `context` (capture phase, so every descendant is seen).
    void Attach(Rml::Context* context);
    void Detach();

    /// Feed one frame of input: pointer moves and button edges in arrival order, wheel, keys, text.
    /// `resync` runs first when commits were drained since the last frame: the owner re-evaluates every
    /// bound value there, so a control whose typed value the action refused shows the model's value again.
    void Forward(const InputState& input, const std::function<void()>& resync = {});

    /// A text control has keyboard focus: chords must not reach AppFlow, camera keys must not act.
    [[nodiscard]] bool WantsKeyboard() const;
    /// The pointer is over a UI element, or a press that began on one is still held.
    [[nodiscard]] bool WantsPointer() const;

    /// Commits queued since the last drain (oldest first). Draining schedules the resync that the next
    /// Forward() performs.
    [[nodiscard]] std::vector<Commit> DrainCommits();

    void ProcessEvent(Rml::Event& event) override;

private:
    [[nodiscard]] Rml::Element* FocusedTextControl() const;
    void QueueCommit(Rml::Element& element);

    Rml::Context* context_ = nullptr;
    std::vector<Commit> commits_;
    bool resyncRequested_ = false;
    // The value the focused control had when it gained focus (what Escape restores, and what a blur
    // compares against so an unedited blur commits nothing).
    std::string valueAtFocus_;
    bool cancelOnBlur_ = false;
    bool pointerPressedOnUi_ = false;
    bool pointerPressedOnWorld_ = false;
    int lastX_ = -1;
    int lastY_ = -1;
    bool havePointer_ = false;
};

}  // namespace Vixen::RenderGraph
