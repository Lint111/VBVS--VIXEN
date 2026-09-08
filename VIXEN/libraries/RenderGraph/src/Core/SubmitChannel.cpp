#include "Core/SubmitChannel.h"

#include <string>

namespace Vixen::RenderGraph {

void SubmitChannel::RecordFault(const char* where, VkResult result) {
    // First fault wins the message (the drain aborts the frame on it); later faults in the same
    // frame are still flagged but do not overwrite the primary cause.
    if (!anyFault_) {
        faultMessage_ = std::string(where ? where : "SubmitChannel") +
                        " failed (VkResult " + std::to_string(static_cast<int>(result)) + ")";
    }
    anyFault_ = true;
}

}  // namespace Vixen::RenderGraph
