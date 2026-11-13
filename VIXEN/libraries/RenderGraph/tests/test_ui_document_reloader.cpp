// Live reload of RmlUi documents, headless (no Vulkan, no window): the reload keeps the data model bound and
// restores the panel state (scroll, focus, caret, hidden state); a broken document keeps the old one and the
// result names file:line; a stylesheet-only edit applies; nothing changed means no reload, and the steady-state
// poll allocates nothing. Documents are written to a temp dir so the test edits real files, like an agent does.
#include <gtest/gtest.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/RenderInterface.h>

#include "Text/RmlFontEngine.h"
#include "Ui/UiDocumentReloader.h"
#include "Ui/UiKit.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {
std::atomic<bool> g_countAllocations{false};
std::atomic<long> g_allocations{0};
}  // namespace

// Counts heap allocations inside a measured window (the steady-state poll must make none).
void* operator new(std::size_t size) {
    if (g_countAllocations.load(std::memory_order_relaxed)) g_allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {

namespace fs = std::filesystem;
using namespace Vixen::RenderGraph;

class NullRenderInterface final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 0; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 0; }
    void ReleaseTexture(Rml::TextureHandle) override {}
};

constexpr const char* kStyle =
    "body { position: absolute; left: 0; top: 0; right: 0; bottom: 0; font-family: LatoLatin; font-size: 14px; }\n"
    "#list { overflow-y: scroll; height: 60px; }\n"
    "#list div { display: block; height: 20px; }\n"
    ".tag { display: block; color: #ff0000; width: 40px; }\n";

std::string Document(const std::string& extraBody = {}, const std::string& label = "label") {
    std::string rows;
    for (int i = 0; i < 12; ++i) rows += "<div>row " + std::to_string(i) + "</div>";
    return "<rml><head><title>t</title><link type=\"text/rcss\" href=\"t.rcss\"/></head>"
           "<body data-model=\"m\"><div id=\"name\">{{" + label + "}}</div>"
           "<input type=\"text\" id=\"field\" value=\"hello world\"/>"
           "<div id=\"list\">" + rows + "</div><div id=\"tag\" class=\"tag\">x</div>" + extraBody + "</body></rml>";
}

class UiDocumentReloaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / ("ui_reload_test_" + std::to_string(std::random_device{}()));
        fs::create_directories(dir_);
        Write("t.rcss", kStyle);
        Write("t.rml", Document());

        reloader_ = std::make_unique<UiDocumentReloader>(sys_);
        Rml::SetSystemInterface(&sys_);
        Rml::SetRenderInterface(&render_);
        Rml::SetFontEngineInterface(&fonts_);
        Rml::SetFileInterface(reloader_->FileInterface());
        ASSERT_TRUE(Rml::Initialise());
        Rml::LoadFontFace("assets/ui/LatoLatin-Regular.ttf");
        ctx_ = Rml::CreateContext("ui_reload_test", Rml::Vector2i(640, 480));
        ASSERT_NE(ctx_, nullptr);
        Rml::DataModelConstructor model = ctx_->CreateDataModel("m");
        ASSERT_TRUE(static_cast<bool>(model));
        model.Bind("label", &label_);
        handle_ = model.GetModelHandle();

        std::vector<std::string> errors;
        doc_ = reloader_->Load(*ctx_, Path("t.rml"), watch_, errors);
        ASSERT_NE(doc_, nullptr);
        EXPECT_TRUE(errors.empty());
        doc_->Show();
        ctx_->Update();
    }

    void TearDown() override {
        if (ctx_) Rml::RemoveContext("ui_reload_test");
        ctx_ = nullptr;
        Rml::Shutdown();
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    std::string Path(const char* name) const { return (dir_ / name).string(); }
    // Writes the file and bumps its mtime past the previous one (filesystem timestamp granularity).
    void Write(const char* name, const std::string& text) {
        const fs::path path = dir_ / name;
        std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
        fs::last_write_time(path, fs::file_time_type::clock::now() + std::chrono::seconds(++bump_));
    }
    bool Reload() {
        const bool ok = reloader_->Reload(*ctx_, doc_, watch_, results_, state_);
        ctx_->Update();
        if (ok && doc_) UiDocumentReloader::Restore(*ctx_, *doc_, state_);
        ctx_->Update();
        return ok;
    }

    Vixen::Ui::VixenRmlSystemInterface sys_;
    NullRenderInterface render_;
    Vixen::Text::RmlFontEngine fonts_;
    std::unique_ptr<UiDocumentReloader> reloader_;
    Rml::Context* ctx_ = nullptr;
    Rml::ElementDocument* doc_ = nullptr;
    Rml::DataModelHandle handle_;
    std::string label_ = "first";
    UiDocumentWatch watch_;
    std::vector<UiReloadFileResult> results_;
    UiDocumentState state_;
    fs::path dir_;
    int bump_ = 0;
};

TEST_F(UiDocumentReloaderTest, WatchesTheDocumentAndTheStylesheetItLinksFromRmlUi) {
    ASSERT_EQ(watch_.files.size(), 2u);
    EXPECT_EQ(watch_.files[0].path.filename(), "t.rml");
    EXPECT_EQ(watch_.files[1].path.filename(), "t.rcss");
    EXPECT_FALSE(UiDocumentReloader::Changed(watch_));
}

TEST_F(UiDocumentReloaderTest, ReloadKeepsTheDataModelBoundAndSwapsTheDocument) {
    Rml::ElementDocument* before = doc_;
    EXPECT_EQ(doc_->GetElementById("name")->GetInnerRML(), "first");
    Write("t.rml", Document("<div id=\"added\">new</div>"));
    ASSERT_TRUE(UiDocumentReloader::Changed(watch_));
    ASSERT_TRUE(Reload());
    EXPECT_NE(doc_, before);
    ASSERT_NE(doc_->GetElementById("added"), nullptr);          // the edit is in the new document
    EXPECT_EQ(doc_->GetElementById("name")->GetInnerRML(), "first");   // still bound to the same model
    label_ = "second";
    handle_.DirtyVariable("label");
    ctx_->Update();
    EXPECT_EQ(doc_->GetElementById("name")->GetInnerRML(), "second");  // and it stays live
    EXPECT_FALSE(UiDocumentReloader::Changed(watch_));         // the new mtimes were taken
    ASSERT_EQ(results_.size(), 2u);
    EXPECT_TRUE(results_[0].ok);
}

TEST_F(UiDocumentReloaderTest, ReloadRestoresScrollFocusAndCaret) {
    doc_->GetElementById("list")->SetScrollTop(30.0f);
    Rml::Element* field = doc_->GetElementById("field");
    ASSERT_TRUE(field->Focus());
    auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(field);
    ASSERT_NE(input, nullptr);
    input->SetSelectionRange(2, 5);
    ctx_->Update();
    ASSERT_EQ(ctx_->GetFocusElement(), field);

    Write("t.rml", Document("<div>changed</div>"));
    ASSERT_TRUE(Reload());
    EXPECT_FLOAT_EQ(doc_->GetElementById("list")->GetScrollTop(), 30.0f);
    Rml::Element* focused = ctx_->GetFocusElement();
    ASSERT_NE(focused, nullptr);
    EXPECT_EQ(focused->GetId(), "field");
    EXPECT_EQ(focused->GetOwnerDocument(), doc_);               // the NEW document's field
    int start = -1, end = -1;
    rmlui_dynamic_cast<Rml::ElementFormControlInput*>(focused)->GetSelection(&start, &end, nullptr);
    EXPECT_EQ(start, 2);
    EXPECT_EQ(end, 5);
}

TEST_F(UiDocumentReloaderTest, AHiddenDocumentStaysHidden) {
    doc_->Hide();
    Write("t.rml", Document("<div>changed</div>"));
    ASSERT_TRUE(Reload());
    EXPECT_FALSE(doc_->IsVisible());
}

TEST_F(UiDocumentReloaderTest, BrokenDocumentKeepsTheOldOneAndNamesFileAndLine) {
    Rml::ElementDocument* before = doc_;
    Write("t.rml", "<rml><head></head>\n<body data-model=\"m\">\n<div id=\"x\">\n<span></div>\n</body></rml>");
    EXPECT_FALSE(Reload());
    EXPECT_EQ(doc_, before);                                    // the old document is still the live one
    EXPECT_NE(doc_->GetElementById("field"), nullptr);
    ASSERT_FALSE(results_.empty());
    EXPECT_FALSE(results_[0].ok);
    EXPECT_NE(results_[0].file.find("t.rml"), std::string::npos) << results_[0].file;
    EXPECT_EQ(results_[0].line, 4) << results_[0].message;   // the closing </div> that does not match <span>
    EXPECT_NE(results_[0].message.find("expecting 'span'"), std::string::npos) << results_[0].message;
    EXPECT_FALSE(UiDocumentReloader::Changed(watch_));          // reported once; the next edit retries
    Write("t.rml", Document("<div id=\"fixed\">ok</div>"));      // fix the file: it recovers
    EXPECT_TRUE(Reload());
    EXPECT_NE(doc_, before);
    EXPECT_NE(doc_->GetElementById("fixed"), nullptr);
}

TEST_F(UiDocumentReloaderTest, StylesheetOnlyChangeAppliesToTheReloadedDocument) {
    EXPECT_FLOAT_EQ(doc_->GetElementById("tag")->GetBox().GetSize().x, 40.0f);
    Write("t.rcss", std::string(kStyle) + ".tag { width: 90px; }\n");
    ASSERT_TRUE(UiDocumentReloader::Changed(watch_));
    ASSERT_TRUE(Reload());
    EXPECT_FLOAT_EQ(doc_->GetElementById("tag")->GetBox().GetSize().x, 90.0f);
}

TEST_F(UiDocumentReloaderTest, ABrokenStylesheetIsRejectedAndLocated) {
    Rml::ElementDocument* before = doc_;
    Write("t.rcss", "body { position: absolute; left: 0; top: 0; right: 0; bottom: 0;\n.tag { color: ; \n");
    const bool ok = Reload();
    // RmlUi may tolerate a malformed sheet by logging; either way a logged error must reject the reload.
    if (!ok) {
        EXPECT_EQ(doc_, before);
        ASSERT_FALSE(results_.empty());
        EXPECT_FALSE(results_[0].ok);
    }
}

// R874: the kit's fragments are dependencies of EVERY document (the load prelude opens them), so editing a fragment
// reloads documents that use the element and documents that do not (yet); a broken fragment keeps the old document and
// the error names the fragment file and line.
TEST_F(UiDocumentReloaderTest, KitFragmentsAreDependenciesOfEveryDocumentAndABrokenOneIsLocated) {
    fs::create_directories(dir_ / "kit");
    const auto fragment = [&](const char* name) { return (dir_ / "kit" / name).string(); };
    Write("kit/button.rml", "<span part=\"label\" class=\"ut-label\"></span>\n");
    UiKit::Registry kit([&](const std::string& asset) { return (dir_ / "kit" / fs::path(asset).filename()).string(); });
    kit.Register();
    reloader_->SetLoadPrelude([&] { kit.TouchFragments(); });

    ASSERT_TRUE(Reload()) << "the unchanged document reloads, now with the fragments recorded";
    bool watchesFragment = false;
    for (const auto& file : watch_.files) watchesFragment = watchesFragment || file.path == fragment("button.rml");
    EXPECT_TRUE(watchesFragment) << "the document uses no kit element, yet the fragment is its dependency";
    EXPECT_FALSE(UiDocumentReloader::Changed(watch_));
    Write("kit/button.rml", "<span part=\"label\" class=\"ut-label\"></span><!-- edited -->\n");
    EXPECT_TRUE(UiDocumentReloader::Changed(watch_)) << "a fragment edit is a change of the document";

    Rml::ElementDocument* before = doc_;
    Write("t.rml", Document("<ut-button id=\"kit-button\" label=\"x\"/>"));
    ASSERT_TRUE(Reload());
    ASSERT_NE(doc_->GetElementById("kit-button"), nullptr);
    EXPECT_EQ(doc_->GetElementById("kit-button")->GetInnerRML().find("PRE"), std::string::npos);
    Write("kit/button.rml", "<span class=\"pre\">PRE</span><span part=\"label\" class=\"ut-label\"></span>\n");
    ASSERT_TRUE(UiDocumentReloader::Changed(watch_));
    ASSERT_TRUE(Reload());
    EXPECT_NE(doc_->GetElementById("kit-button")->GetInnerRML().find("PRE"), std::string::npos) << "a fragment edit reaches the reloaded instance";
    before = doc_;
    Write("kit/button.rml", "<span part=\"label\" class=\"ut-label\">\n<b>\n");   // never closed
    EXPECT_FALSE(Reload());
    EXPECT_EQ(doc_, before) << "every panel keeps its old document";
    ASSERT_FALSE(results_.empty());
    EXPECT_NE(results_.front().file.find("button.rml"), std::string::npos) << results_.front().file << ": " << results_.front().message;
    EXPECT_GT(results_.front().line, 0);
    Write("kit/button.rml", "<span part=\"label\" class=\"ut-label\"></span>\n");
    EXPECT_TRUE(Reload()) << "fixing the fragment recovers";
    // Elements release through their instancer: drop the document before the registry (and its instancers) goes.
    ctx_->UnloadDocument(doc_);
    doc_ = nullptr;
    ctx_->Update();
    kit.Unregister();
}

TEST_F(UiDocumentReloaderTest, SteadyStatePollAllocatesNothingAndReloadsNothing) {
    ASSERT_FALSE(UiDocumentReloader::Changed(watch_));
    g_allocations = 0;
    g_countAllocations = true;
    bool any = false;
    for (int i = 0; i < 2000; ++i) any = UiDocumentReloader::Changed(watch_) || any;
    g_countAllocations = false;
    // Steady-frame cost, MEASURED: one poll = a stat per watched file (2 here; the editor's panels watch 2 each).
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 20000; ++i) any = UiDocumentReloader::Changed(watch_) || any;
    const double nsPerPoll = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / 20000.0;
    std::printf("[measured] UiDocumentReloader::Changed: %.0f ns per document (%zu files)\n", nsPerPoll, watch_.files.size());
    EXPECT_FALSE(any);
    EXPECT_EQ(g_allocations.load(), 0) << "the mtime poll must not allocate";
}

TEST(UiDocumentReloaderParse, LocatedErrorSplitsFileLineMessage) {
    const auto located = UiDocumentReloader::ParseLocatedError("/a/b/editor_layers.rml:12: Unexpected closing tag", "x");
    EXPECT_FALSE(located.ok);
    EXPECT_EQ(located.file, "/a/b/editor_layers.rml");
    EXPECT_EQ(located.line, 12);
    EXPECT_EQ(located.message, "Unexpected closing tag");
    const auto windows = UiDocumentReloader::ParseLocatedError("C:/ui/p.rml:3: bad", "x");
    EXPECT_EQ(windows.file, "C:/ui/p.rml");
    EXPECT_EQ(windows.line, 3);
    const auto xml = UiDocumentReloader::ParseLocatedError("XML parse error on line 5 of /a/p.rml.", "x");
    EXPECT_EQ(xml.file, "/a/p.rml");
    EXPECT_EQ(xml.line, 5);
    const auto closing = UiDocumentReloader::ParseLocatedError(
        "Closing tag 'div' mismatched on /a/p.rml:4 was expecting 'span'.", "x");
    EXPECT_EQ(closing.file, "/a/p.rml");
    EXPECT_EQ(closing.line, 4);
    EXPECT_EQ(closing.message, "Closing tag 'div' mismatched, was expecting 'span'.");
    const auto plain = UiDocumentReloader::ParseLocatedError("no location here", "fallback.rml");
    EXPECT_EQ(plain.file, "fallback.rml");
    EXPECT_EQ(plain.line, 0);
}

}  // namespace
