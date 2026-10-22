#pragma once

// Compatibility include for code that historically got the R723 protocol from
// VulkanResources. The protocol itself belongs to EventBus so lower-level producers do not
// depend on the VulkanResources device implementation.
#include "../../EventBus/include/QueueSubmitRequest.h"
