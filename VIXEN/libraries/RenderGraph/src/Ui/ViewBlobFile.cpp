#include "Ui/ViewBlobFile.h"
#include <RmlUi/Core/Log.h>
#include <charconv>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace Vixen::RenderGraph {

static bool ScalarKind(std::string_view t, ViewKind& out) {
    if (t == "int") { out = ViewKind::Int; return true; }
    if (t == "float") { out = ViewKind::Float; return true; }
    if (t == "bool") { out = ViewKind::Bool; return true; }
    if (t == "string") { out = ViewKind::String; return true; }
    return false;
}

std::optional<ViewBlobFile> ViewBlobFile::Parse(std::string_view text) {
    ViewBlobFile f;
    std::string model; uint32_t version = 0; bool haveModel = false, haveVersion = false;
    std::unordered_map<std::string, size_t> elemIndex;   // elem name -> index into elemArrays_
    std::istringstream in{std::string(text)};
    std::string line;
    std::vector<ViewFieldDesc>* curElemVec = nullptr;
    auto intern = [&](std::string s) -> std::string_view { f.strings_.push_back(std::move(s)); return f.strings_.back(); };

    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        bool indented = (line[0] == ' ');
        std::istringstream ls{line};
        std::string tok; ls >> tok;
        if (indented) {
            if (!curElemVec) { Rml::Log::Message(Rml::Log::LT_ERROR, "viewblob: indented field outside elem"); return std::nullopt; }
            std::string kindTok; ls >> kindTok; ViewKind k;
            if (!ScalarKind(kindTok, k)) { Rml::Log::Message(Rml::Log::LT_ERROR, "viewblob: bad elem kind '%s'", kindTok.c_str()); return std::nullopt; }
            curElemVec->push_back(ViewFieldDesc{intern(tok), k, {}});
            continue;
        }
        curElemVec = nullptr;
        if (tok == "model") { ls >> model; haveModel = true; }
        else if (tok == "version") {
            std::string hx; ls >> hx;
            if (hx.rfind("0x", 0) != 0) { Rml::Log::Message(Rml::Log::LT_ERROR, "viewblob: bad version"); return std::nullopt; }
            auto r = std::from_chars(hx.data()+2, hx.data()+hx.size(), version, 16);
            if (r.ec != std::errc{}) { Rml::Log::Message(Rml::Log::LT_ERROR, "viewblob: bad version hex"); return std::nullopt; }
            haveVersion = true;
        }
        else if (tok == "elem") {
            std::string name; ls >> name;
            f.elemArrays_.emplace_back();
            elemIndex[name] = f.elemArrays_.size() - 1;
            curElemVec = &f.elemArrays_.back();
        }
        else if (tok == "field") {
            std::string name, kindTok; ls >> name >> kindTok;
            if (kindTok == "array") {
                std::string elemName; ls >> elemName;
                auto it = elemIndex.find(elemName);
                if (it == elemIndex.end()) { Rml::Log::Message(Rml::Log::LT_ERROR, "viewblob: array '%s' references undeclared elem '%s'", name.c_str(), elemName.c_str()); return std::nullopt; }
                std::span<const ViewFieldDesc> elem{f.elemArrays_[it->second]};
                f.topFields_.push_back(ViewFieldDesc{intern(name), ViewKind::ArrayOfStruct, elem});
            } else {
                ViewKind k;
                if (!ScalarKind(kindTok, k)) { Rml::Log::Message(Rml::Log::LT_ERROR, "viewblob: bad field kind '%s'", kindTok.c_str()); return std::nullopt; }
                f.topFields_.push_back(ViewFieldDesc{intern(name), k, {}});
            }
        }
        else { Rml::Log::Message(Rml::Log::LT_ERROR, "viewblob: unknown directive '%s'", tok.c_str()); return std::nullopt; }
    }
    if (!haveModel || !haveVersion) { Rml::Log::Message(Rml::Log::LT_ERROR, "viewblob: missing model/version"); return std::nullopt; }
    f.blob_ = ViewBlob{ intern(model), std::span<const ViewFieldDesc>{f.topFields_}, version };
    return f;
}

std::optional<ViewBlobFile> ViewBlobFile::Load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { Rml::Log::Message(Rml::Log::LT_ERROR, "viewblob: cannot open '%s'", path.c_str()); return std::nullopt; }
    std::ostringstream ss; ss << in.rdbuf();
    return Parse(ss.str());
}

}  // namespace Vixen::RenderGraph
