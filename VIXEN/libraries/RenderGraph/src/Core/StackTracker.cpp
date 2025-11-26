#include "Core/StackTracker.h"
#include <sstream>
#include <iomanip>
#include <iostream>

namespace Vixen::RenderGraph {

void StackTracker::OnWarningThreshold(std::string_view name, size_t size) {
    std::cerr << "[STACK WARNING] Stack usage exceeded warning threshold!\n"
              << "  Allocation: " << name << " (" << FormatBytes(size) << ")\n"
              << "  Current usage: " << FormatBytes(currentUsage) << "\n"
              << "  Peak usage: " << FormatBytes(peakUsage) << "\n"
              << "  Threshold: " << FormatBytes(STACK_WARNING_THRESHOLD) << "\n"
              << "  Consider reducing MAX_* constants in VulkanLimits.h\n";
}

void StackTracker::OnCriticalThreshold(std::string_view name, size_t size) {
    std::cerr << "[STACK CRITICAL] Stack usage exceeded critical threshold!\n"
              << "  Allocation: " << name << " (" << FormatBytes(size) << ")\n"
              << "  Current usage: " << FormatBytes(currentUsage) << "\n"
              << "  Peak usage: " << FormatBytes(peakUsage) << "\n"
              << "  Threshold: " << FormatBytes(STACK_CRITICAL_THRESHOLD) << "\n"
              << "  DANGER: Risk of stack overflow!\n";

    // Print detailed allocations to help diagnose
    PrintAllocations();
}

void StackTracker::PrintStatistics() const {
    if constexpr (!STACK_TRACKER_ENABLED) {
        std::cout << "[StackTracker] Disabled in release build\n";
        return;
    }

    std::cout << "\n=== Stack Tracker Statistics ===\n"
              << "Current Frame:\n"
              << "  Current usage:    " << FormatBytes(currentUsage) << "\n"
              << "  Peak usage:       " << FormatBytes(peakUsage) << "\n"
              << "  Allocations:      " << allocationCount << "\n"
              << "\nLifetime:\n"
              << "  Peak usage:       " << FormatBytes(lifetimePeakUsage) << "\n"
              << "  Total allocs:     " << lifetimeAllocationCount << "\n"
              << "  Frames tracked:   " << frameCount << "\n"
              << "\nThresholds:\n"
              << "  Warning:          " << FormatBytes(STACK_WARNING_THRESHOLD);

    if (lifetimePeakUsage >= STACK_WARNING_THRESHOLD) {
        std::cout << " [EXCEEDED]";
    }

    std::cout << "\n  Critical:         " << FormatBytes(STACK_CRITICAL_THRESHOLD);

    if (lifetimePeakUsage >= STACK_CRITICAL_THRESHOLD) {
        std::cout << " [EXCEEDED]";
    }

    std::cout << "\n\nEstimated overhead: " << FormatBytes(ESTIMATED_MAX_STACK_PER_FRAME) << " per frame\n";

    // Calculate percentage of critical threshold
    double percentage = (static_cast<double>(lifetimePeakUsage) / STACK_CRITICAL_THRESHOLD) * 100.0;
    std::cout << "Peak usage is " << std::fixed << std::setprecision(1) << percentage
              << "% of critical threshold\n";

    std::cout << "================================\n\n";
}

void StackTracker::PrintAllocations() const {
    if constexpr (!STACK_TRACKER_ENABLED) return;

    std::cout << "\n=== Stack Allocation Details ===\n";
    std::cout << std::left << std::setw(50) << "Allocation Name"
              << std::right << std::setw(12) << "Size"
              << std::setw(16) << "Cumulative\n";
    std::cout << std::string(78, '-') << "\n";

    for (size_t i = 0; i < recordIndex; ++i) {
        const auto& record = allocations[i];
        std::cout << std::left << std::setw(50) << record.name
                  << std::right << std::setw(12) << FormatBytes(record.size)
                  << std::setw(16) << FormatBytes(record.cumulativeSize) << "\n";
    }

    if (recordIndex >= MAX_RECORDED_ALLOCATIONS) {
        std::cout << "(Additional allocations not recorded - buffer full)\n";
    }

    std::cout << "================================\n\n";
}

std::string StackTracker::FormatBytes(size_t bytes) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    if (bytes >= 1024 * 1024 * 1024) {
        oss << (bytes / (1024.0 * 1024.0 * 1024.0)) << " GB";
    } else if (bytes >= 1024 * 1024) {
        oss << (bytes / (1024.0 * 1024.0)) << " MB";
    } else if (bytes >= 1024) {
        oss << (bytes / 1024.0) << " KB";
    } else {
        oss << bytes << " B";
    }

    return oss.str();
}

} // namespace Vixen::RenderGraph
