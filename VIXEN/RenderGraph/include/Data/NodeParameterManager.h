#pragma once

#include "ParameterDataTypes.h"
#include <map>
#include <string>
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
