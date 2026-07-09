#pragma once

#include "ParameterDataTypes.h"
#include <cstdio>
#include <limits>
#include <map>
#include <string>
#include <type_traits>
#include <variant>

namespace Vixen::RenderGraph {

/**
 * @brief Manages node instance parameters
 *
 * Encapsulates parameter storage and access for NodeInstance.
 * Provides type-safe parameter get/set operations.
 */
class NodeParameterManager {
public:
    NodeParameterManager() = default;
    ~NodeParameterManager() = default;

    // Prevent copying
    NodeParameterManager(const NodeParameterManager&) = delete;
    NodeParameterManager& operator=(const NodeParameterManager&) = delete;

    /**
     * @brief Set parameter value
     * @param name Parameter name
     * @param value Parameter value (variant type)
     */
    void SetParameter(const std::string& name, const ParamTypeValue& value) {
        parameters[name] = value;
    }

    /**
     * @brief Get parameter value (raw variant)
     * @param name Parameter name
     * @return Pointer to parameter value, or nullptr if not found
     */
    const ParamTypeValue* GetParameter(const std::string& name) const {
        auto it = parameters.find(name);
        if (it == parameters.end()) {
            return nullptr;
        }
        return &it->second;
    }

    /**
     * @brief Get typed parameter value with default fallback
     * @tparam T Parameter type
     * @param name Parameter name
     * @param defaultValue Default value if not found or wrong type
     * @return Parameter value or default
     */
    template<typename T>
    T GetParameterValue(const std::string& name, const T& defaultValue = T{}) const {
        auto it = parameters.find(name);
        if (it == parameters.end()) {
            return defaultValue;
        }

        if (auto* value = std::get_if<T>(&it->second)) {
            return *value;
        }

        // int32<->uint32 is the canonical silent failure: call sites store int literals,
        // nodes read uint32_t (see Widescreen-Perf-Sweep-Findings-2026-07 D1 — this
        // defaulted every fresh window to 800x600). Convert when the value is in range.
        if constexpr (std::is_same_v<T, uint32_t>) {
            if (auto* v = std::get_if<int32_t>(&it->second)) {
                if (*v >= 0) return static_cast<uint32_t>(*v);
            }
        } else if constexpr (std::is_same_v<T, int32_t>) {
            if (auto* v = std::get_if<uint32_t>(&it->second)) {
                if (*v <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
                    return static_cast<int32_t>(*v);
            }
        }

        // A silent default here cost weeks of "why is the window 800x600" — be loud.
        std::fprintf(stderr,
            "[NodeParameterManager] WARNING: parameter '%s' stored with a different type "
            "than requested — returning default\n", name.c_str());
        return defaultValue;
    }

    /**
     * @brief Check if parameter exists
     * @param name Parameter name
     * @return true if parameter exists
     */
    bool HasParameter(const std::string& name) const {
        return parameters.find(name) != parameters.end();
    }

    /**
     * @brief Clear all parameters
     */
    void Clear() {
        parameters.clear();
    }

    /**
     * @brief Get parameter count
     * @return Number of parameters
     */
    size_t GetParameterCount() const {
        return parameters.size();
    }

private:
    std::map<std::string, ParamTypeValue> parameters;
};

} // namespace Vixen::RenderGraph
