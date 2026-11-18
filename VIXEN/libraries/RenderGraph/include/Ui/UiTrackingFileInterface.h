#pragma once
// The file interface the UI hosts install: RmlUi's stdio behaviour, plus a record of every path RmlUi
// opens while a document loads. That record is how the live reload learns which files a document pulled
// in (its own .rml, the stylesheets it links or @imports, nested imports) from RmlUi itself rather than
// from a hand list or a directory scan. Tracking is off except between BeginTrack and EndTrack, so the
// steady state costs one branch per Open.
#include <RmlUi/Core/FileInterface.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace Vixen::Ui {

class UiTrackingFileInterface final : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String& path) override {
        const bool trace = std::getenv("VIXEN_EDITOR_EXEC_TRACE") != nullptr;
        if (trace) std::cout << "[UIFileTrace] open.begin path=" << path << std::endl;
        FILE* file = std::fopen(path.c_str(), "rb");
        if (trace) std::cout << "[UIFileTrace] open.end path=" << path << " ok=" << (file != nullptr) << std::endl;
        if (file && tracking_) opened_.push_back(path);
        return reinterpret_cast<Rml::FileHandle>(file);
    }
    void Close(Rml::FileHandle file) override {
        if (file) std::fclose(reinterpret_cast<FILE*>(file));
        if (std::getenv("VIXEN_EDITOR_EXEC_TRACE")) std::cout << "[UIFileTrace] close" << std::endl;
    }
    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override {
        return std::fread(buffer, 1, size, reinterpret_cast<FILE*>(file));
    }
    bool Seek(Rml::FileHandle file, long offset, int origin) override {
        return std::fseek(reinterpret_cast<FILE*>(file), offset, origin) == 0;
    }
    size_t Tell(Rml::FileHandle file) override { return static_cast<size_t>(std::ftell(reinterpret_cast<FILE*>(file))); }

    void BeginTrack() { opened_.clear(); tracking_ = true; }
    /// Paths opened since BeginTrack, in open order, duplicates removed.
    std::vector<std::string> EndTrack() {
        tracking_ = false;
        std::vector<std::string> unique;
        for (auto& path : opened_) {
            bool seen = false;
            for (const auto& have : unique) if (have == path) { seen = true; break; }
            if (!seen) unique.push_back(std::move(path));
        }
        opened_.clear();
        return unique;
    }

private:
    bool tracking_ = false;
    std::vector<std::string> opened_;
};

}  // namespace Vixen::Ui
