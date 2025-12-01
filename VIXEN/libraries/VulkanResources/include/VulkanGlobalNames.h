#pragma once

#include <vector>

// Provide weak (selectany) definitions for test-friendly global Vulkan option lists.
// Using __declspec(selectany) allows test TUs to provide their own definitions
// without causing multiple-definition LINK errors on MSVC.

#ifdef _MSC_VER
#define VIXEN_SELECTANY __declspec(selectany)
#else
// On non-MSVC compilers, map to inline variables (C++17) as an alternative.
#define VIXEN_SELECTANY inline
#endif

VIXEN_SELECTANY std::vector<const char*> deviceExtensionNames;
VIXEN_SELECTANY std::vector<const char*> layerNames;
VIXEN_SELECTANY std::vector<const char*> instanceExtensionNames;
