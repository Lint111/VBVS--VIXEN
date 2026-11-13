#include "Ui/UiDocumentReloader.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Factory.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace Vixen::RenderGraph {

namespace {

void Collect(Rml::Element& element, std::vector<UiDocumentState::Scroll>& out) {
    if (!element.GetId().empty()) {
        const float top = element.GetScrollTop();
        const float left = element.GetScrollLeft();
        if (top != 0.0f || left != 0.0f) out.push_back({element.GetId(), top, left});
    }
    for (int i = 0; i < element.GetNumChildren(true); ++i) {
        if (Rml::Element* child = element.GetChild(i)) Collect(*child, out);
    }
}

}  // namespace

void UiDocumentReloader::Stamp(UiDocumentWatch& watch) {
    for (auto& file : watch.files) {
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(file.path, ec);
        if (!ec) file.mtime = time;
    }
}

Rml::ElementDocument* UiDocumentReloader::Load(Rml::Context& context, const std::string& path,
                                               UiDocumentWatch& watch, std::vector<std::string>& errors) {
    system_.BeginCapture();
    files_.BeginTrack();
    Rml::ElementDocument* document = context.LoadDocument(path);
    const std::vector<std::string> opened = files_.EndTrack();
    errors = system_.EndCapture();

    watch.path = path;
    watch.files.clear();
    watch.files.reserve(opened.size());
    for (const auto& file : opened) watch.files.push_back({std::filesystem::path(file), {}});
    Stamp(watch);
    return document;
}

bool UiDocumentReloader::Changed(const UiDocumentWatch& watch) {
    for (const auto& file : watch.files) {
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(file.path, ec);
        if (!ec && time != file.mtime) return true;
    }
    return false;
}

UiReloadFileResult UiDocumentReloader::ParseLocatedError(const std::string& message, const std::string& fallbackFile) {
    UiReloadFileResult result;
    result.ok = false;
    result.file = fallbackFile;
    result.message = message;
    const auto parseInt = [&](size_t from, size_t& end) {
        end = from;
        while (end < message.size() && std::isdigit(static_cast<unsigned char>(message[end]))) ++end;
        return end > from ? std::atoi(message.substr(from, end - from).c_str()) : -1;
    };
    // (1) "<file>:<line>: <message>" (Rml::Log::ParseError). The first ':<digits>: ' wins, so a drive-letter
    //     colon in the file part is not taken for the separator.
    for (size_t colon = message.find(':'); colon != std::string::npos; colon = message.find(':', colon + 1)) {
        size_t end = 0;
        const int line = parseInt(colon + 1, end);
        if (line >= 0 && end + 1 < message.size() && message[end] == ':' && message[end + 1] == ' ') {
            result.file = message.substr(0, colon);
            result.line = line;
            result.message = message.substr(end + 2);
            return result;
        }
    }
    // (2) "XML parse error on line <line> of <file>."
    static const std::string kXml = "XML parse error on line ";
    if (message.rfind(kXml, 0) == 0) {
        size_t end = 0;
        const int line = parseInt(kXml.size(), end);
        static const std::string kOf = " of ";
        if (line >= 0 && message.compare(end, kOf.size(), kOf) == 0) {
            result.file = message.substr(end + kOf.size());
            if (!result.file.empty() && result.file.back() == '.') result.file.pop_back();
            result.line = line;
            result.message = "XML parse error";
            return result;
        }
    }
    // (3) "<what> on <file>:<line> <rest>" (e.g. "Closing tag 'div' mismatched on f.rml:4 was expecting 'span'.").
    static const std::string kOn = " on ";
    for (size_t on = message.find(kOn); on != std::string::npos; on = message.find(kOn, on + 1)) {
        const size_t fileStart = on + kOn.size();
        for (size_t colon = message.find(':', fileStart); colon != std::string::npos;
             colon = message.find(':', colon + 1)) {
            size_t end = 0;
            const int line = parseInt(colon + 1, end);
            if (line < 0) continue;
            result.file = message.substr(fileStart, colon - fileStart);
            result.line = line;
            result.message = message.substr(0, on) + (end < message.size() ? "," + message.substr(end) : std::string{});
            return result;
        }
    }
    return result;
}

bool UiDocumentReloader::Reload(Rml::Context& context, Rml::ElementDocument*& doc, UiDocumentWatch& watch,
                                std::vector<UiReloadFileResult>& results, UiDocumentState& state) {
    results.clear();
    state = {};

    // A changed stylesheet must be re-read from disk, not served from RmlUi's cache.
    Rml::Factory::ClearStyleSheetCache();
    UiDocumentWatch fresh;
    std::vector<std::string> errors;
    Rml::ElementDocument* next = Load(context, watch.path, fresh, errors);
    if (next) {
        // No focus change from Show(): the old focus (if any) is restored by id below, never stolen.
        next->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
        // A body with only out-of-flow children collapses to 0x0 and renders nothing (UIRenderNode::MountNow).
        if (next->GetOffsetWidth() <= 0.0f || next->GetOffsetHeight() <= 0.0f)
            errors.push_back(watch.path + ": document has a degenerate 0-size layout");
    } else if (errors.empty()) {
        errors.push_back(watch.path + ": RmlUi could not load the document");
    }

    if (!next || !errors.empty()) {
        if (next) context.UnloadDocument(next);
        Stamp(watch);  // consume the broken mtimes: reported once, retried on the next edit
        for (const auto& error : errors) results.push_back(ParseLocatedError(error, watch.path));
        // Lead with the most useful entry: a located one, then the longest message (RmlUi logs a terse
        // "XML parse error on line N" and then the descriptive line).
        std::stable_sort(results.begin(), results.end(), [](const UiReloadFileResult& a, const UiReloadFileResult& b) {
            if ((a.line > 0) != (b.line > 0)) return a.line > 0;
            return a.message.size() > b.message.size();
        });
        return false;
    }

    if (doc) {
        Collect(*doc, state.scrolls);
        Rml::Element* focus = context.GetFocusElement();
        if (focus && focus->GetOwnerDocument() == doc && !focus->GetId().empty()) {
            state.focusId = focus->GetId();
            if (auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(focus))
                input->GetSelection(&state.selectionStart, &state.selectionEnd, nullptr);
        }
        if (!doc->IsVisible()) next->Hide();
        context.UnloadDocument(doc);
    }
    state.framesLeft = state.Empty() ? 0 : 3;
    doc = next;
    watch.files = std::move(fresh.files);
    for (const auto& file : watch.files) {
        UiReloadFileResult ok;
        ok.file = file.path.string();
        results.push_back(std::move(ok));
    }
    return true;
}

bool UiDocumentReloader::Restore(Rml::Context& context, Rml::ElementDocument& doc, UiDocumentState& state) {
    if (state.framesLeft <= 0) return true;
    --state.framesLeft;
    bool complete = true;
    for (const auto& scroll : state.scrolls) {
        Rml::Element* element = doc.GetElementById(scroll.id);
        if (!element) { complete = false; continue; }
        element->SetScrollTop(scroll.top);
        element->SetScrollLeft(scroll.left);
    }
    if (!state.focusId.empty()) {
        Rml::Element* element = doc.GetElementById(state.focusId);
        if (!element) {
            complete = false;
        } else {
            if (context.GetFocusElement() != element) element->Focus();
            if (auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(element);
                input && state.selectionStart >= 0)
                input->SetSelectionRange(state.selectionStart, state.selectionEnd);
        }
    }
    if (complete) state.framesLeft = 0;
    return state.framesLeft <= 0;
}

}  // namespace Vixen::RenderGraph
