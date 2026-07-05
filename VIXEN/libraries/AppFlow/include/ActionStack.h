#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>
#include "AppFlowResults.h"
#include "generated/AppFlow.g.h"

namespace Vixen::AppFlow {

using Generated::FlowActionId;
using Generated::AppFlowActionDecl;

// Reversible-action primitive (design §3.1 ActionStack; §5 "A user gesture, end to end").
// Inc-1: the caller supplies a forward/inverse toggle callback per dispatch (ApplyFn) — the
// real opcode VM / snapshot-fallback path is Inc 2. forward=true applies, false inverts.
class ActionStack {
public:
    using ApplyFn = std::function<void(bool /*forward*/)>;

    void LoadActions(const AppFlowActionDecl* table, size_t count);

    // Opens a group; dispatches until the next EndGroup() are recorded together so one
    // Undo()/Redo() reverts/reapplies them as one unit (one gesture = one undoable unit).
    void BeginGroup(uint32_t group);
    void EndGroup();

    // Runs apply(true), records the entry under the current (or an auto-opened singleton)
    // group, and clears the redo stack (a new dispatch invalidates the redo branch).
    // Unknown action id (not present in the loaded table) → RejectedByState, nothing recorded.
    DispatchResult Dispatch(FlowActionId id, ApplyFn apply);

    // Pops the last group from the undo stack onto the redo stack, running each entry's
    // apply(false) in reverse order. NothingToUndo if the undo stack is empty.
    DispatchResult Undo();
    // Pops the last group from the redo stack onto the undo stack, running each entry's
    // apply(true) in forward order. NothingToRedo if the redo stack is empty.
    DispatchResult Redo();

    size_t UndoDepth() const { return undo_.size(); }
    size_t RedoDepth() const { return redo_.size(); }

private:
    struct Entry { FlowActionId id; ApplyFn apply; };
    struct Group { uint32_t id; std::vector<Entry> entries; };

    bool IsKnownAction(FlowActionId id) const;

    std::vector<AppFlowActionDecl> actions_;
    std::vector<Group> undo_;
    std::vector<Group> redo_;
    bool hasOpenGroup_ = false;
    Group openGroup_{};
};

} // namespace Vixen::AppFlow
