#pragma once
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>
#include <vulkan/vulkan.h>
namespace ShaderInterface { namespace ExposureMeter {
enum class Access : uint32_t { ReadWrite=0, ReadOnly=1, WriteOnly=2 };
struct MemberInfo { const char* name; bool isPushMember; uint32_t set; uint32_t binding; uint32_t offset; Access access; uint32_t featureCount; const char* const* features; };
using MembersArray = MemberInfo[2];
inline constexpr MemberInfo MEMBERS[] = {
 {"sceneRadianceHistory", false, 0, 0, 0, Access::ReadOnly, 0, nullptr},
 {"result", false, 0, 1, 0, Access::ReadWrite, 0, nullptr},
};
inline std::vector<MemberInfo> Members(const std::unordered_set<std::string>&) { return {MEMBERS[0], MEMBERS[1]}; }
struct Metadata { static constexpr const char* PROGRAM_NAME="ExposureMeter"; static constexpr uint32_t NUM_MEMBERS=2; static constexpr uint32_t NUM_FEATURES=0; };
namespace Bind { struct sceneRadianceHistory { static constexpr uint32_t SET=0, BINDING=0; }; struct result { static constexpr uint32_t SET=0, BINDING=1; }; }
namespace Push { static constexpr uint32_t SIZE=0; }
}}
