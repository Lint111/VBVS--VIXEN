#pragma once
// VixenRmlSystemInterface — minimal Rml::SystemInterface. RmlUi only strictly needs elapsed time;
// logging keeps RmlUi's default (stderr). Header-only.
#include <RmlUi/Core/SystemInterface.h>

#include <chrono>

namespace Vixen::Ui {

class VixenRmlSystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

} // namespace Vixen::Ui
