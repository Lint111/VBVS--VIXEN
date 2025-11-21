#pragma once

/**
 * ArrayView.h - Compatibility header
 *
 * This file provides type aliases for std::span from C++20.
 * We use std::span instead of a custom implementation since it's
 * available in C++23 and provides the exact functionality we need.
 *
 * Usage:
 *   #include "ArrayView.h"
 *   ArrayView<float> view = ...;  // Uses std::span<float>
 */

#include <span>

namespace VoxelData {

// Type aliases for convenience
template<typename T>
using ArrayView = std::span<T>;

template<typename T>
using ConstArrayView = std::span<const T>;

} // namespace VoxelData
