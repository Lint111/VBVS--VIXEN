#pragma once

#include <cctype>
#include <cstdlib>

namespace Vixen::Core::Env {

// Environment flags have historically treated any non-whitespace value as enabled.  Keep the
// explicit 0/1 parser separate so callers can distinguish an unset flag from an explicit value.
inline bool Enabled(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) return false;
    for (; *value != '\0'; ++value) {
        if (!std::isspace(static_cast<unsigned char>(*value))) return true;
    }
    return false;
}

inline bool IsSet(const char* name, bool& value) {
    const char* raw = std::getenv(name);
    if (raw == nullptr) return false;
    while (*raw != '\0' && std::isspace(static_cast<unsigned char>(*raw))) ++raw;
    if (*raw == '0') {
        value = false;
        return true;
    }
    if (*raw == '1') {
        value = true;
        return true;
    }
    return false;
}

inline bool EnabledOrDefault(const char* name, bool defaultValue) {
    const char* raw = std::getenv(name);
    if (raw == nullptr) return defaultValue;

    bool explicitValue = false;
    if (IsSet(name, explicitValue)) return explicitValue;
    return Enabled(name);
}

}  // namespace Vixen::Core::Env
