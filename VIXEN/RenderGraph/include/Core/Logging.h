#pragma once

#include <iostream>
#include <string>
#include <sstream>

/**
 * @file Logging.h
 * @brief Simple logging macros for core render graph components
 *
 * Provides basic logging functionality without requiring ILoggable interface.
 * Used by standalone components like AliasingEngine, ResourceLifetimeAnalyzer, etc.
 */

// Enable/disable logging at compile time
#ifndef VIXEN_ENABLE_LOGGING
#define VIXEN_ENABLE_LOGGING 1
#endif

#if VIXEN_ENABLE_LOGGING

/**
 * @brief Log debug message (verbose)
 */
#define LOG_DEBUG(msg) \
    do { \
        std::cout << "[DEBUG] " << msg << std::endl; \
    } while(0)

/**
 * @brief Log info message (important events)
 */
#define LOG_INFO(msg) \
    do { \
        std::cout << "[INFO] " << msg << std::endl; \
    } while(0)

/**
 * @brief Log warning message (recoverable issues)
 */
#define LOG_WARNING(msg) \
    do { \
        std::cout << "[WARNING] " << msg << std::endl; \
    } while(0)

/**
 * @brief Log error message (failures)
 */
#define LOG_ERROR(msg) \
    do { \
        std::cerr << "[ERROR] " << msg << std::endl; \
    } while(0)

/**
 * @brief Log critical message (fatal errors)
 */
#define LOG_CRITICAL(msg) \
    do { \
        std::cerr << "[CRITICAL] " << msg << std::endl; \
    } while(0)

#else

// Logging disabled - no-op macros
#define LOG_DEBUG(msg) do {} while(0)
#define LOG_INFO(msg) do {} while(0)
#define LOG_WARNING(msg) do {} while(0)
#define LOG_ERROR(msg) do {} while(0)
#define LOG_CRITICAL(msg) do {} while(0)

#endif // VIXEN_ENABLE_LOGGING
