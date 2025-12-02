#pragma once

// ============================================================================
// SVO (Sparse Voxel Octree) Precompiled Header
// ============================================================================
// Common headers for Laine-Karras octree implementation with compression

// Platform Configuration
#define NOMINMAX

// Third-party - Mathematics
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Standard Library - Containers
#include <vector>
#include <array>
#include <queue>
#include <deque>
#include <unordered_map>
#include <map>
#include <set>

// Standard Library - Utilities
#include <string>
#include <string_view>
#include <memory>
#include <functional>
#include <optional>
#include <variant>
#include <type_traits>
#include <algorithm>

// Standard Library - Synchronization
#include <mutex>
#include <atomic>
#include <thread>

// Standard Library - I/O
#include <iostream>
#include <sstream>
#include <fstream>

// Standard Library - Math/Science
#include <cmath>
#include <limits>
#include <numeric>

// Standard Library - Other
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <chrono>
