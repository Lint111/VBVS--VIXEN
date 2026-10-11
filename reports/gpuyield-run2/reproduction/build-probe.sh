set -euo pipefail
sdk="$PWD/VIXEN/.vulkan-sdk/1.4.350.1/x86_64"
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers -I"$sdk/include" tools/gpu-cadence-probe.cpp -L"$sdk/lib/VulkanLoader/lib" -lvulkan -pthread -o .tmp/gpuyield-run2/gpu-cadence-probe
