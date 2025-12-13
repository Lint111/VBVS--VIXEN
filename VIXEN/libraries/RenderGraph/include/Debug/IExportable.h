#pragma once

#include <string>
#include <ostream>
#include <fstream>
#include <vector>

namespace Vixen::RenderGraph::Debug {

/**
 * @brief Interface for types that can be serialized to various formats
 *
 * Any data structure that needs to be exported for analysis, logging,
 * or debugging should implement this interface.
 */
class IExportable {
public:
    virtual ~IExportable() = default;

    /**
     * @brief Convert to human-readable string
     * @return String representation suitable for console/log output
     */
    virtual std::string ToString() const = 0;

    /**
     * @brief Convert to CSV format (single row, no header)
     * @return CSV-formatted string
     */
    virtual std::string ToCSV() const = 0;

    /**
     * @brief Get CSV header for this type
     * @return CSV header row with column names
     */
    virtual std::string GetCSVHeader() const = 0;

    /**
     * @brief Convert to JSON format
     * @return JSON-formatted string
     */
    virtual std::string ToJSON() const = 0;
};

/**
 * @brief Utility to export collections of data
 *
 * NOTE: Types used with this exporter must have ToString(), ToCSV(),
 * GetCSVHeader(), and ToJSON() methods. These don't need to be virtual.
 */
class Exporter {
public:
    /**
     * @brief Export samples to console/log
     */
    template<typename T>
    static void ToConsole(const std::vector<T>& samples, size_t maxSamples = 100) {
        size_t count = std::min(samples.size(), maxSamples);
        for (size_t i = 0; i < count; ++i) {
            printf("[%zu] %s\n", i, samples[i].ToString().c_str());
        }
        if (samples.size() > maxSamples) {
            printf("... and %zu more samples\n", samples.size() - maxSamples);
        }
    }

    /**
     * @brief Export samples to CSV file
     */
    template<typename T>
    static bool ToCSVFile(const std::vector<T>& samples, const std::string& filename) {
        std::ofstream file(filename);
        if (!file.is_open()) return false;

        // Write header
        if (!samples.empty()) {
            file << samples[0].GetCSVHeader() << "\n";
        }

        // Write data rows
        for (const auto& sample : samples) {
            file << sample.ToCSV() << "\n";
        }

        return true;
    }

    /**
     * @brief Export samples to JSON file
     */
    template<typename T>
    static bool ToJSONFile(const std::vector<T>& samples, const std::string& filename) {
        std::ofstream file(filename);
        if (!file.is_open()) return false;

        file << "[\n";
        for (size_t i = 0; i < samples.size(); ++i) {
            file << "  " << samples[i].ToJSON();
            if (i < samples.size() - 1) file << ",";
            file << "\n";
        }
        file << "]\n";

        return true;
    }
};

} // namespace Vixen::RenderGraph::Debug
