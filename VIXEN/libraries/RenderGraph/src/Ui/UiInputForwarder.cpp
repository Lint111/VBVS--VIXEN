#include "Ui/UiInputForwarder.h"

#define GLFW_INCLUDE_NONE   // keyboard constants only: no <GL/gl.h> on headless/WSL
#include <GLFW/glfw3.h>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Event.h>

namespace Vixen::RenderGraph {

namespace {

bool IsTextControl(const Rml::Element* element) {
    if (element == nullptr) return false;
    const Rml::String& tag = element->GetTagName();
    if (tag == "textarea") return true;
    if (tag != "input") return false;
    const Rml::String type = element->GetAttribute<Rml::String>("type", "text");
    return type == "text" || type == "password" || type == "number";
}

Rml::String ValueOf(Rml::Element& element) {
    return element.GetAttribute<Rml::String>("value", "");
}

// Appends one code point as UTF-8.
void AppendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

}  // namespace

Rml::Input::KeyIdentifier RmlKeyFromGlfw(int key) {
    using namespace Rml::Input;
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) return static_cast<KeyIdentifier>(KI_A + (key - GLFW_KEY_A));
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) return static_cast<KeyIdentifier>(KI_0 + (key - GLFW_KEY_0));
    if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9) return static_cast<KeyIdentifier>(KI_NUMPAD0 + (key - GLFW_KEY_KP_0));
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) return static_cast<KeyIdentifier>(KI_F1 + (key - GLFW_KEY_F1));
    switch (key) {
        case GLFW_KEY_SPACE: return KI_SPACE;
        case GLFW_KEY_BACKSPACE: return KI_BACK;
        case GLFW_KEY_TAB: return KI_TAB;
        case GLFW_KEY_ENTER: return KI_RETURN;
        case GLFW_KEY_KP_ENTER: return KI_NUMPADENTER;
        case GLFW_KEY_ESCAPE: return KI_ESCAPE;
        case GLFW_KEY_LEFT: return KI_LEFT;
        case GLFW_KEY_RIGHT: return KI_RIGHT;
        case GLFW_KEY_UP: return KI_UP;
        case GLFW_KEY_DOWN: return KI_DOWN;
        case GLFW_KEY_HOME: return KI_HOME;
        case GLFW_KEY_END: return KI_END;
        case GLFW_KEY_PAGE_UP: return KI_PRIOR;
        case GLFW_KEY_PAGE_DOWN: return KI_NEXT;
        case GLFW_KEY_INSERT: return KI_INSERT;
        case GLFW_KEY_DELETE: return KI_DELETE;
        case GLFW_KEY_SEMICOLON: return KI_OEM_1;
        case GLFW_KEY_EQUAL: return KI_OEM_PLUS;
        case GLFW_KEY_COMMA: return KI_OEM_COMMA;
        case GLFW_KEY_MINUS: return KI_OEM_MINUS;
        case GLFW_KEY_PERIOD: return KI_OEM_PERIOD;
        case GLFW_KEY_SLASH: return KI_OEM_2;
        case GLFW_KEY_GRAVE_ACCENT: return KI_OEM_3;
        case GLFW_KEY_LEFT_BRACKET: return KI_OEM_4;
        case GLFW_KEY_BACKSLASH: return KI_OEM_5;
        case GLFW_KEY_RIGHT_BRACKET: return KI_OEM_6;
        case GLFW_KEY_APOSTROPHE: return KI_OEM_7;
        case GLFW_KEY_KP_DECIMAL: return KI_DECIMAL;
        default: return KI_UNKNOWN;
    }
}

int RmlModifiersFromGlfw(int mods) {
    int out = 0;
    if (mods & GLFW_MOD_CONTROL) out |= Rml::Input::KM_CTRL;
    if (mods & GLFW_MOD_SHIFT) out |= Rml::Input::KM_SHIFT;
    if (mods & GLFW_MOD_ALT) out |= Rml::Input::KM_ALT;
    if (mods & GLFW_MOD_SUPER) out |= Rml::Input::KM_META;
    if (mods & GLFW_MOD_CAPS_LOCK) out |= Rml::Input::KM_CAPSLOCK;
    if (mods & GLFW_MOD_NUM_LOCK) out |= Rml::Input::KM_NUMLOCK;
    return out;
}

bool HasValueSlot(const std::string& elementId) {
    const size_t open = elementId.rfind('{');
    return open != std::string::npos && elementId.find('}', open) != std::string::npos;
}

std::string SelectorFromCommittedId(const std::string& elementId, const std::string& text) {
    const size_t open = elementId.rfind('{');
    const size_t close = open == std::string::npos ? std::string::npos : elementId.find('}', open);
    if (close == std::string::npos) return elementId;
    return elementId.substr(0, open) + text + elementId.substr(close + 1);
}

void UiInputForwarder::Attach(Rml::Context* context) {
    if (context_ == context) return;
    Detach();
    context_ = context;
    if (!context_) return;
    // Capture phase on the context: the listener sees focus/blur/change for every descendant, including
    // documents mounted later, with no per-document registration.
    context_->AddEventListener("focus", this, true);
    context_->AddEventListener("blur", this, true);
    context_->AddEventListener("change", this, true);
}

void UiInputForwarder::Detach() {
    if (context_) {
        context_->RemoveEventListener("focus", this, true);
        context_->RemoveEventListener("blur", this, true);
        context_->RemoveEventListener("change", this, true);
    }
    context_ = nullptr;
}

Rml::Element* UiInputForwarder::FocusedTextControl() const {
    Rml::Element* focus = context_ ? context_->GetFocusElement() : nullptr;
    return IsTextControl(focus) ? focus : nullptr;
}

bool UiInputForwarder::WantsKeyboard() const { return FocusedTextControl() != nullptr; }

bool UiInputForwarder::WantsPointer() const {
    if (!context_) return false;
    if (pointerPressedOnUi_) return true;
    if (pointerPressedOnWorld_) return false;  // a drag that began outside the UI stays the world's
    Rml::Element* hover = context_->GetHoverElement();
    return hover != nullptr && hover != context_->GetRootElement();
}

void UiInputForwarder::QueueCommit(Rml::Element& element) {
    const std::string id = element.GetId();
    if (id.empty()) return;
    const std::string text = ValueOf(element);
    commits_.push_back({id, SelectorFromCommittedId(id, text), text});
    valueAtFocus_ = text;  // a following blur compares against the committed text: one commit per edit
}

void UiInputForwarder::ProcessEvent(Rml::Event& event) {
    Rml::Element* target = event.GetTargetElement();
    if (!IsTextControl(target)) return;
    switch (event.GetId()) {
        case Rml::EventId::Focus:
            valueAtFocus_ = ValueOf(*target);
            cancelOnBlur_ = false;
            break;
        case Rml::EventId::Change:
            // Enter in a single-line control arrives as change{linebreak = true}: commit, then release
            // focus so key chords resume. Every other change is just typing.
            if (event.GetParameter<bool>("linebreak", false)) {
                QueueCommit(*target);
                target->Blur();
            }
            break;
        case Rml::EventId::Blur:
            if (!cancelOnBlur_ && ValueOf(*target) != valueAtFocus_) QueueCommit(*target);
            cancelOnBlur_ = false;
            break;
        default: break;
    }
}

std::vector<UiInputForwarder::Commit> UiInputForwarder::DrainCommits() {
    std::vector<Commit> out = std::move(commits_);
    commits_.clear();
    if (!out.empty()) resyncRequested_ = true;
    return out;
}

void UiInputForwarder::Forward(const InputState& input, const std::function<void()>& resync) {
    if (!context_) return;
    if (resyncRequested_) {
        resyncRequested_ = false;
        if (resync) resync();
    }
    int modifiers = 0;  // refreshed from each key event; pointer events reuse the latest seen
    if (!input.keyEventsThisFrame.empty()) modifiers = RmlModifiersFromGlfw(input.keyEventsThisFrame.back().mods);

    const auto move = [&](float x, float y) {
        const int ix = static_cast<int>(x), iy = static_cast<int>(y);
        if (havePointer_ && ix == lastX_ && iy == lastY_) return;
        lastX_ = ix; lastY_ = iy; havePointer_ = true;
        context_->ProcessMouseMove(ix, iy, modifiers);
    };

    // Button edges carry the cursor position at the press, so each is preceded by a move to it.
    for (const ClickEvent& click : input.clicksThisFrame) {
        move(click.x, click.y);
        if (click.pressed) {
            // The first press of a gesture decides who owns it until every button is released: on a UI element
            // (hover is a real element, not the context root) it stays a UI drag even off the panel; anywhere
            // else it stays the world's even over a panel.
            Rml::Element* hover = context_->GetHoverElement();
            if (!pointerPressedOnUi_ && !pointerPressedOnWorld_) {
                if (hover != nullptr && hover != context_->GetRootElement()) pointerPressedOnUi_ = true;
                else pointerPressedOnWorld_ = true;
            }
            context_->ProcessMouseButtonDown(click.button, modifiers);
        } else {
            context_->ProcessMouseButtonUp(click.button, modifiers);
        }
    }
    move(input.mousePosition.x, input.mousePosition.y);
    if (!input.mouseButtons[0] && !input.mouseButtons[1] && !input.mouseButtons[2]) {
        pointerPressedOnUi_ = false;
        pointerPressedOnWorld_ = false;
    }

    if (input.wheelDelta.x != 0.0f || input.wheelDelta.y != 0.0f) {
        // GLFW's positive y is "away from the user"; RmlUi's positive y scrolls content down.
        context_->ProcessMouseWheel(Rml::Vector2f(-input.wheelDelta.x, -input.wheelDelta.y), modifiers);
    }

    for (const RawKeyEvent& key : input.keyEventsThisFrame) {
        const Rml::Input::KeyIdentifier id = RmlKeyFromGlfw(key.glfwKey);
        if (id == Rml::Input::KI_UNKNOWN) continue;
        const int mods = RmlModifiersFromGlfw(key.mods);
        if (key.action == GLFW_RELEASE) {
            context_->ProcessKeyUp(id, mods);
            continue;
        }
        // Escape in a text control cancels the edit: put the value back, then drop focus.
        if (id == Rml::Input::KI_ESCAPE && key.action == GLFW_PRESS) {
            if (Rml::Element* field = FocusedTextControl()) {
                static_cast<Rml::ElementFormControl*>(field)->SetValue(valueAtFocus_);
                cancelOnBlur_ = true;
                field->Blur();
                continue;
            }
            cancelPressed_ = true;  // nothing owned the key: the generic cancel
        }
        context_->ProcessKeyDown(id, mods);
    }

    for (const char32_t cp : input.textThisFrame) {
        // Control characters are key events, not text.
        if (cp < 0x20 || cp == 0x7F) continue;
        std::string utf8;
        AppendUtf8(utf8, cp);
        context_->ProcessTextInput(utf8);
    }
}

}  // namespace Vixen::RenderGraph
