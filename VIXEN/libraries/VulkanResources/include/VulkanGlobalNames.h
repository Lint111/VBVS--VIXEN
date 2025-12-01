#pragma once

#include <vector>

// Provide weak definitions for global Vulkan option lists.
// Using C++17 inline variables ensures single definition across translation units.
// Note: __declspec(selectany) doesn't work with non-trivial types like std::vector.

inline std::vector<const char*> deviceExtensionNames;
inline std::vector<const char*> layerNames;
inline std::vector<const char*> instanceExtensionNames;
