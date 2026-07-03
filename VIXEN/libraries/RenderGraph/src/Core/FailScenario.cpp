#include "Core/FailScenario.h"
#if defined(VIXEN_FAIL_SCENARIOS) && VIXEN_FAIL_SCENARIOS
namespace Vixen::RenderGraph::FailScenario {

ScenarioRegistry& ScenarioRegistry::Instance() { static ScenarioRegistry r; return r; }

void ScenarioRegistry::Register(std::string name, std::vector<ScenarioDecl> decls) {
    for (auto& e : entries_)
        if (e.first == name) { e.second = std::move(decls); return; }  // idempotent replay
    entries_.emplace_back(std::move(name), std::move(decls));
}
const std::vector<ScenarioDecl>* ScenarioRegistry::Find(const std::string& name) const {
    for (const auto& e : entries_) if (e.first == name) return &e.second;
    return nullptr;
}
void ScenarioRegistry::ForEach(const std::function<void(const std::string&, const ScenarioDecl&)>& fn) const {
    for (const auto& e : entries_) for (const auto& d : e.second) fn(e.first, d);
}

namespace detail {
    std::vector<std::function<void()>>& ScenarioRegistrars() {
        static std::vector<std::function<void()>> v; return v;
    }
    bool AddScenarioRegistrar(std::function<void()> thunk) {
        ScenarioRegistrars().push_back(std::move(thunk)); return true;
    }
}
void ReplayScenarioRegistrars() {
    for (auto& t : detail::ScenarioRegistrars()) t();  // Register() is replace-idempotent
}

void FaultInjector::ArmOnce(FaultSite s, VkResult f) { slots_[Idx(s)] = { true, f }; }
VkResult FaultInjector::Filter(FaultSite s, VkResult real) {
    auto& a = slots_[Idx(s)];
    if (!a.armed) return real;
    a.armed = false;
    return a.forced;
}
bool FaultInjector::IsArmed(FaultSite s) const { return slots_[Idx(s)].armed; }

namespace {
    std::atomic<uint32_t>& ValidationErrorCounter() {
        static std::atomic<uint32_t> counter{0};
        return counter;
    }
}
uint32_t ValidationErrorCount() { return ValidationErrorCounter().load(std::memory_order_relaxed); }
void ResetValidationErrorCount() { ValidationErrorCounter().store(0, std::memory_order_relaxed); }
namespace detail { void BumpValidationError() { ValidationErrorCounter().fetch_add(1, std::memory_order_relaxed); } }

} // namespace
#endif
