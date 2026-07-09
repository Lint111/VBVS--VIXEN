#include "ActionStack.h"
#include <algorithm>
#include <cstring>

namespace Vixen::AppFlow {

void ActionStack::LoadActions(const AppFlowActionDecl* table, size_t count) {
    actions_.assign(table, table + count);
}

bool ActionStack::IsKnownAction(FlowActionId id) const {
    return std::any_of(actions_.begin(), actions_.end(),
                        [id](const AppFlowActionDecl& d) { return d.id == id; });
}

void ActionStack::BeginGroup(uint32_t group) {
    openGroup_ = Group{group, {}};
    hasOpenGroup_ = true;
}

void ActionStack::EndGroup() {
    if (hasOpenGroup_) {
        undo_.push_back(std::move(openGroup_));
        openGroup_ = Group{};
        hasOpenGroup_ = false;
    }
}

DispatchResult ActionStack::Dispatch(FlowActionId id, ApplyFn apply) {
    if (!IsKnownAction(id)) {
        return DispatchResult::RejectedByState;
    }

    apply(true);
    redo_.clear();

    if (hasOpenGroup_) {
        // Caller has an explicit BeginGroup()/EndGroup() bracket open — accumulate into it.
        openGroup_.entries.push_back(Entry{id, std::move(apply)});
    } else {
        // No explicit group — auto-open a singleton group for this one dispatch.
        Group g{0, {}};
        g.entries.push_back(Entry{id, std::move(apply)});
        undo_.push_back(std::move(g));
    }
    return DispatchResult::Ok;
}

DispatchResult ActionStack::DispatchWithSnapshot(FlowActionId id, void* footprint,
                                                  uint32_t footprintBytes, ApplyFn apply,
                                                  std::function<void()> onRestore) {
    if (!IsKnownAction(id)) {
        return DispatchResult::RejectedByState;
    }

    Entry entry{id, apply};
    entry.footprint = footprint;
    entry.footprintBytes = footprintBytes;
    entry.snapshot.resize(footprintBytes);
    std::memcpy(entry.snapshot.data(), footprint, footprintBytes);
    entry.onRestore = std::move(onRestore);

    apply(true);
    redo_.clear();

    if (hasOpenGroup_) {
        // Caller has an explicit BeginGroup()/EndGroup() bracket open — accumulate into it.
        openGroup_.entries.push_back(std::move(entry));
    } else {
        // No explicit group — auto-open a singleton group for this one dispatch.
        Group g{0, {}};
        g.entries.push_back(std::move(entry));
        undo_.push_back(std::move(g));
    }
    return DispatchResult::Ok;
}

DispatchResult ActionStack::Undo() {
    if (undo_.empty()) {
        return DispatchResult::NothingToUndo;
    }
    Group g = std::move(undo_.back());
    undo_.pop_back();
    for (auto it = g.entries.rbegin(); it != g.entries.rend(); ++it) {
        if (it->IsSnapshot()) {
            std::memcpy(it->footprint, it->snapshot.data(), it->footprintBytes);
            if (it->onRestore) it->onRestore();
        } else {
            it->apply(false);
        }
    }
    redo_.push_back(std::move(g));
    return DispatchResult::Ok;
}

DispatchResult ActionStack::Redo() {
    if (redo_.empty()) {
        return DispatchResult::NothingToRedo;
    }
    Group g = std::move(redo_.back());
    redo_.pop_back();
    for (auto& entry : g.entries) {
        entry.apply(true);
    }
    undo_.push_back(std::move(g));
    return DispatchResult::Ok;
}

} // namespace Vixen::AppFlow
