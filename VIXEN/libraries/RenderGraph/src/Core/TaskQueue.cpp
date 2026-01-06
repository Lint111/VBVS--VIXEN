/**
 * @file TaskQueue.cpp
 * @brief Explicit template instantiations for TaskQueue
 *
 * Sprint 6.2: TaskQueue System - Task #339
 * Design Element: #37 TaskQueue System
 *
 * This file provides explicit template instantiations to:
 * 1. Reduce compile times (template code compiled once)
 * 2. Verify template compiles with primary use case (DispatchPass)
 * 3. Ensure linker can find symbols when used across translation units
 *
 * Additional instantiations can be added as new TTaskData types are needed.
 */

#include "Core/TaskQueue.h"
#include "Data/DispatchPass.h"

namespace Vixen::RenderGraph {

// ============================================================================
// Explicit Template Instantiations
// ============================================================================

// Primary use case: DispatchPass for MultiDispatchNode
template class TaskQueue<DispatchPass>;

// Future instantiations (Task #341, #342 may add more):
// template class TaskQueue<ComputeTask>;
// template class TaskQueue<TransferTask>;

}  // namespace Vixen::RenderGraph
