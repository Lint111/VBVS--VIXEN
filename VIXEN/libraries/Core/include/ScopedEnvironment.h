#pragma once

// Portable process-environment mutation for tests and tools. POSIX setenv/unsetenv do not exist
// on MSVC and _putenv_s has different clearing semantics, so every fixture goes through this one
// header instead of hand-rolling an #ifdef _WIN32 branch.

#include <cstdlib>
#include <optional>
#include <string>

namespace Vixen::Core::Env {

// Sets `name` to `value` (overwriting). Passing nullptr clears the variable.
inline void Set(const char* name, const char* value) {
#ifdef _WIN32
    // _putenv_s with "" removes the variable on the MSVC CRT.
    _putenv_s(name, value != nullptr ? value : "");
#else
    if (value != nullptr) {
        ::setenv(name, value, 1);
    } else {
        ::unsetenv(name);
    }
#endif
}

inline void Set(const char* name, const std::string& value) { Set(name, value.c_str()); }

inline void Unset(const char* name) { Set(name, static_cast<const char*>(nullptr)); }

// Sets (or, with nullptr, clears) a variable for the lifetime of the object and restores the
// previous state on destruction.
class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* name, const char* value) : name_(name) {
        if (const char* previous = std::getenv(name_)) {
            previous_ = std::string(previous);
        }
        Set(name_, value);
    }

    ~ScopedEnvironment() {
        Set(name_, previous_ ? previous_->c_str() : nullptr);
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

private:
    const char* name_;
    std::optional<std::string> previous_;
};

}  // namespace Vixen::Core::Env
