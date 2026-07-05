#pragma once
namespace Vixen::AppFlow {
enum class LoadResult { Ok, EmptyArtifact, BadTransitionRef, UnknownAction };
enum class DispatchResult { Ok, RejectedByState, GuardFailed, NothingToUndo, NothingToRedo };
} // namespace Vixen::AppFlow
