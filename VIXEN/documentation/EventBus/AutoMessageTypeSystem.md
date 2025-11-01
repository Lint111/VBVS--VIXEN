# Auto-Incrementing Message Type System

**Created**: November 1, 2025
**Status**: Production

## Overview

The auto-incrementing message type system prevents MessageType ID collisions by automatically assigning unique IDs at compile time using the `__COUNTER__` macro.

## Problem Solved

**Previous Issue**: Manual MessageType assignment led to collisions:
```cpp
// Message.h
struct DeviceMetadataEvent {
    static constexpr MessageType TYPE = 106;  // Manually assigned
};

// RenderGraphEvents.h
struct CleanupRequestedMessage {
    static constexpr MessageType TYPE = 106;  // COLLISION! Same ID
};
```

**Result**: DeviceMetadataEvent triggered CleanupRequestedMessage handlers, causing unexpected graph cleanup.

## Solution

### Implementation

**Location**: `EventBus/include/EventBus/Message.h` (lines 30-58)

```cpp
namespace detail {
    // Base offset for auto-generated IDs (start at 1000 to avoid manual IDs)
    constexpr MessageType MESSAGE_TYPE_BASE = 1000;

    template<int N>
    struct NextMessageType {
        static constexpr MessageType value = MESSAGE_TYPE_BASE + N;
    };
}

// Helper macro for declaring message types with auto-increment
#define AUTO_MESSAGE_TYPE() (::Vixen::EventBus::detail::MESSAGE_TYPE_BASE + __COUNTER__)
```

### Usage

**All message types now use AUTO_MESSAGE_TYPE():**

```cpp
struct MyMessage : public BaseEventMessage {
    static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE();
    static constexpr EventCategory CATEGORY = EventCategory::System;

    MyMessage(SenderID sender)
        : BaseEventMessage(CATEGORY, TYPE, sender) {}
};
```

**Each invocation gets a unique ID:**
```cpp
struct MessageA { static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE(); }; // 1000
struct MessageB { static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE(); }; // 1001
struct MessageC { static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE(); }; // 1002
```

## How It Works

1. **__COUNTER__ macro**: Compiler-provided macro that increments each time it's evaluated
   - Starts at 0
   - Increments by 1 per use in a translation unit
   - Compile-time only (zero runtime cost)

2. **Base offset**: Types 0-999 reserved for special cases, auto-generated start at 1000

3. **Template wrapper**: `NextMessageType<N>` provides type-safe value storage

4. **Macro convenience**: `AUTO_MESSAGE_TYPE()` wraps the calculation

## ID Ranges

| Range | Usage |
|-------|-------|
| 0-1 | Reserved (TextMessage=0, WorkerResultMessage=1) |
| 2-999 | Reserved for future manual assignment |
| 1000+ | Auto-generated via AUTO_MESSAGE_TYPE() |

## Benefits

1. **No Collisions**: Guaranteed unique IDs per translation unit
2. **No Tracking**: No need to check what numbers are taken
3. **Compile-Time**: All IDs resolved at compile time
4. **Zero Cost**: No runtime overhead
5. **Type Safe**: Compile-time errors if misused

## Migration

**All existing message types converted:**

### Message.h (EventBus)
- WindowResizeEvent
- WindowStateChangeEvent
- SwapChainInvalidatedEvent
- PipelineInvalidatedEvent
- RenderPauseEvent
- ShutdownAckEvent
- DeviceMetadataEvent

### RenderGraphEvents.h (RenderGraph)
- WindowCloseEvent
- SwapChainRecreatedEvent
- ShaderHotReloadEvent
- CleanupRequestedMessage
- CleanupCompletedMessage
- DeviceSyncRequestedMessage
- NodeRecompileRequestedMessage

## Best Practices

### ✅ DO
```cpp
// Use AUTO_MESSAGE_TYPE() for all new messages
struct NewMessage : public BaseEventMessage {
    static constexpr MessageType TYPE = AUTO_MESSAGE_TYPE();
    // ...
};
```

### ❌ DON'T
```cpp
// Don't manually assign IDs (unless in reserved range 0-1)
struct BadMessage : public BaseEventMessage {
    static constexpr MessageType TYPE = 1050;  // Will collide with auto-generated!
    // ...
};
```

## Debug Tips

**Finding a message type's actual ID:**
```cpp
// At compile time - will show in error message
static_assert(MyMessage::TYPE == 9999, "Show me the type!");

// At runtime - log or debugger
std::cout << "MyMessage TYPE = " << MyMessage::TYPE << std::endl;
```

**Checking for collisions:**
The compiler doesn't prevent duplicate TYPE values, but MessageBus will dispatch incorrectly. Use unique names and AUTO_MESSAGE_TYPE() to avoid issues.

## Future Enhancements

Possible improvements:
1. Static registry to detect collisions at compile time
2. Consteval function for compile-time collision checking
3. Type ID -> name mapping for debugging

## Related Files

- `EventBus/include/EventBus/Message.h` - Auto-increment system implementation
- `EventBus/include/EventBus/MessageBus.h` - Message dispatching by TYPE
- `RenderGraph/include/EventTypes/RenderGraphEvents.h` - RenderGraph message types
