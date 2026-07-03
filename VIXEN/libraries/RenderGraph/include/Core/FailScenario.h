#pragma once
// Fail-scenario declarations (design: Fail-Scenario-Simulation-Design-2026-07.md).
// EVERYTHING here is compiled out when VIXEN_FAIL_SCENARIOS is off — the OFF branch
// defines only empty macros so node .cpps can declare scenarios unconditionally.
#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS

#include <vulkan/vulkan.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>

struct GLFWwindow;  // handle-only, same pattern as WindowNode.h

namespace Vixen::RenderGraph {
class RenderGraph;
class SwapChainNode;  // contracts return node pointers; the declaring .cpp includes the real header

namespace FailScenario {

enum class FaultSite : uint8_t { Acquire, Present, FenceWait };

struct VkTransient    { FaultSite site; VkResult result; };
struct WindowStimulus { enum class Kind : uint8_t { ResizeTo, Maximize, Minimize, Restore };
                        Kind kind; uint32_t width = 0, height = 0; };

// Abstract on purpose: contract lambdas compile inside ENGINE node TUs, so this interface must
// carry zero test/gtest/VixenApp dependency. The sweep harness implements it (ScenarioContextImpl).
// Contracts report via Fail()/Skip() — never gtest macros in library code.
class ScenarioContext {
public:
    virtual ~ScenarioContext() = default;
    virtual bool RunFrames(uint32_t n) = 0;
    virtual void ArmFault(FaultSite site, VkResult result) = 0;
    virtual bool ApplyStimulus(const WindowStimulus& ws) = 0;  // false = WM refused the op
    virtual uint32_t ValidationErrors() const = 0;
    virtual SwapChainNode* SwapChain() = 0;
    virtual GLFWwindow* Window() = 0;
    virtual RenderGraph* Graph() = 0;
    virtual void Fail(const std::string& msg) = 0;
    [[noreturn]] virtual void Skip(const std::string& msg) = 0;
};
using Contract = std::function<void(ScenarioContext&)>;

struct ScenarioDecl {
    std::string id;
    std::variant<WindowStimulus, VkTransient> stimulus;
    Contract contract;
    const char* knownIssueId = nullptr;  // set → runner reports the failure but does not gate on it
};

// Variadic so designated-init braces (which contain commas) pass through into a real
// C++ function-call parse — same trick as forwarding __VA_ARGS__ into a call.
template <typename Stim>
inline ScenarioDecl MakeDecl(const char* id, Stim stim, Contract c, const char* knownIssue = nullptr) {
    return ScenarioDecl{ id, std::move(stim), std::move(c), knownIssue };
}

class ScenarioRegistry {
public:
    static ScenarioRegistry& Instance();
    void Register(std::string nodeTypeName, std::vector<ScenarioDecl> decls);
    const std::vector<ScenarioDecl>* Find(const std::string& nodeTypeName) const;
    void ForEach(const std::function<void(const std::string&, const ScenarioDecl&)>& fn) const;
private:
    std::vector<std::pair<std::string, std::vector<ScenarioDecl>>> entries_;
};

namespace detail {
    // Mirrors NodeRegistration.h: Meyers-singleton thunk list, appended at dynamic-init,
    // replayed on demand. Node TUs are whole-archived (RenderGraphNodes), so registrars
    // placed in them are never linker-stripped — same guarantee VIXEN_REGISTER_NODE relies on.
    std::vector<std::function<void()>>& ScenarioRegistrars();
    bool AddScenarioRegistrar(std::function<void()> thunk);
}
void ReplayScenarioRegistrars();  // idempotent

class FaultInjector {  // Task 2 wires it into the graph; declared here so RenderGraph.h needs one include
public:
    void ArmOnce(FaultSite site, VkResult forced);
    VkResult Filter(FaultSite site, VkResult real);
    bool IsArmed(FaultSite site) const;
private:
    struct Armed { bool armed = false; VkResult forced = VK_SUCCESS; };
    Armed slots_[3];
    static constexpr size_t Idx(FaultSite s) { return static_cast<size_t>(s); }
};

// Global validation-error counter (Task 4 Step 4): bumped by InstanceNode's VK_EXT_debug_report
// callback whenever the layer reports VK_DEBUG_REPORT_ERROR_BIT_EXT. The sweep runner resets it
// before each scenario and asserts it stayed 0 (global pass criterion 2).
uint32_t ValidationErrorCount();
void ResetValidationErrorCount();
namespace detail { void BumpValidationError(); }

} // namespace FailScenario
} // namespace Vixen::RenderGraph

#define VIXEN_FAIL_SCENARIO_CONCAT2(a, b) a##b
#define VIXEN_FAIL_SCENARIO_CONCAT(a, b) VIXEN_FAIL_SCENARIO_CONCAT2(a, b)

#define VIXEN_SCENARIO(id, ...) \
    ::Vixen::RenderGraph::FailScenario::MakeDecl(#id, __VA_ARGS__)

#define VIXEN_FAIL_SCENARIOS_DECLARE(NodeTypeClass, ...)                                   \
    namespace {                                                                            \
        const bool VIXEN_FAIL_SCENARIO_CONCAT(s_vixen_fail_scenarios_, __COUNTER__) =      \
            ::Vixen::RenderGraph::FailScenario::detail::AddScenarioRegistrar([]() {        \
                ::Vixen::RenderGraph::FailScenario::ScenarioRegistry::Instance().Register( \
                    NodeTypeClass().GetTypeName(), { __VA_ARGS__ });                       \
            });                                                                            \
    }

// resultExpr is variadic so real calls with commas in their argument lists pass through intact.
#define VIXEN_FAULT_FILTER(graphPtr, site, ...)                                            \
    ((graphPtr) != nullptr                                                                 \
        ? (graphPtr)->GetFaultInjector()->Filter(                                          \
              ::Vixen::RenderGraph::FailScenario::FaultSite::site, (__VA_ARGS__))          \
        : (__VA_ARGS__))

#else  // ─────────── VIXEN_FAIL_SCENARIOS off: zero footprint ───────────

#define VIXEN_SCENARIO(id, ...)
#define VIXEN_FAIL_SCENARIOS_DECLARE(NodeTypeClass, ...)
#define VIXEN_FAULT_FILTER(graphPtr, site, ...) (__VA_ARGS__)

#endif
