#pragma once
namespace Vixen::AppFlow {
enum class LoadResult { Ok, EmptyArtifact, BadTransitionRef, BadStateMarkers, DuplicateEdgeId, UnknownAction };
enum class DispatchResult { Ok, RejectedByState, GuardFailed, NothingToUndo, NothingToRedo,
                            // R874.3: the action is declared in the loaded data but no handler is registered (inert until code lands).
                            NoHandler };
} // namespace Vixen::AppFlow
