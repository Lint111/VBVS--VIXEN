#pragma once
// VixenRmlSystemInterface — minimal Rml::SystemInterface. RmlUi only strictly needs elapsed time;
// logging keeps RmlUi's default (stderr), and can additionally be captured around one document load so the
// live reload can report the "file:line: message" RmlUi logged for a bad document. Header-only.
#include <RmlUi/Core/SystemInterface.h>

#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace Vixen::Ui {

class VixenRmlSystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (capturing_ && (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT)) captured_.push_back(message);
        return Rml::SystemInterface::LogMessage(type, message);
    }

    /// Error-level messages logged between BeginCapture and EndCapture (still also printed).
    void BeginCapture() { captured_.clear(); capturing_ = true; }
    std::vector<std::string> EndCapture() { capturing_ = false; return std::exchange(captured_, {}); }

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    bool capturing_ = false;
    std::vector<std::string> captured_;
};

} // namespace Vixen::Ui
