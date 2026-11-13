#pragma once
// Live reload of RmlUi documents (generic, every app: UIRenderNode owns one). A document is watched
// through the files RmlUi itself opened while loading it (UiTrackingFileInterface): its .rml and every
// stylesheet it links or @imports. A change loads the new document beside the old one, checks it,
// restores the panel state (scroll, focus, caret, visibility), swaps, and only then drops the old one.
// A document that does not parse (or logs an error, or lays out empty) is discarded: the old document
// stays, and the result names file:line. The data model is Context-level, so it stays bound.
//
// The steady state is a stat of each watched file at a low cadence (UIRenderNode: every 15 ticks) and
// allocates nothing.
#include "Ui/UiTrackingFileInterface.h"
#include "Ui/VixenRmlSystemInterface.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Rml { class Context; class ElementDocument; }

namespace Vixen::RenderGraph {

/// One file of a reload: `ok` true, or the located error (`line`/`column` 0 when RmlUi gave none).
struct UiReloadFileResult {
    std::string file;
    bool ok = true;
    int line = 0;
    int column = 0;
    std::string message;
};

struct UiWatchedFile {
    std::filesystem::path path;
    std::filesystem::file_time_type mtime{};
};

/// The document a watch belongs to and every file RmlUi opened to load it.
struct UiDocumentWatch {
    std::string path;
    std::vector<UiWatchedFile> files;
};

/// What a panel had at the moment of a swap, to re-apply to the new document once its data-bound rows
/// exist (the next Context::Update). Elements are found again by id.
struct UiDocumentState {
    struct Scroll { std::string id; float top = 0.0f; float left = 0.0f; };
    std::vector<Scroll> scrolls;
    std::string focusId;
    int selectionStart = -1;
    int selectionEnd = -1;
    int framesLeft = 0;  ///< Restore attempts remaining (rows can appear a frame late)
    [[nodiscard]] bool Empty() const { return scrolls.empty() && focusId.empty(); }
};

class UiDocumentReloader {
public:
    explicit UiDocumentReloader(Vixen::Ui::VixenRmlSystemInterface& system) : system_(system) {}

    /// Install with Rml::SetFileInterface before Rml::Initialise.
    Rml::FileInterface* FileInterface() { return &files_; }

    /// Loads `path` and records, from RmlUi's own file opens, the files it needs (mtimes stamped now).
    /// Returns the document (null if RmlUi failed) and the error-level messages RmlUi logged meanwhile.
    Rml::ElementDocument* Load(Rml::Context& context, const std::string& path, UiDocumentWatch& watch,
                               std::vector<std::string>& errors);

    /// True when any watched file's mtime moved. Allocation-free; a file that cannot be stat'ed (mid-save)
    /// counts as unchanged and is retried at the next poll.
    [[nodiscard]] static bool Changed(const UiDocumentWatch& watch);

    /// Reloads `doc` from disk. On success: `doc` is the new document (the old one is unloaded), `state`
    /// holds what to restore, `results` lists every watched file ok. On failure: `doc` and `watch` are
    /// untouched except the mtimes (the broken state is reported once, the next edit retries), `results`
    /// names the failing file with line/message. Returns success.
    bool Reload(Rml::Context& context, Rml::ElementDocument*& doc, UiDocumentWatch& watch,
                std::vector<UiReloadFileResult>& results, UiDocumentState& state);

    /// Re-applies `state` to `doc`; true when everything it names was found (or attempts ran out).
    static bool Restore(Rml::Context& context, Rml::ElementDocument& doc, UiDocumentState& state);

    /// Splits an RmlUi "file:line: message" log line (the file may itself contain ':').
    static UiReloadFileResult ParseLocatedError(const std::string& message, const std::string& fallbackFile);

private:
    static void Stamp(UiDocumentWatch& watch);

    Vixen::Ui::VixenRmlSystemInterface& system_;
    Vixen::Ui::UiTrackingFileInterface files_;
};

}  // namespace Vixen::RenderGraph
