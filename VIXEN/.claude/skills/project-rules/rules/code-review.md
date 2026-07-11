# Code Review Rules

<rule id="code-review" reiterate="task-relevant:code-review">

## Approach

**Senior developer mentoring junior** - honest critical feedback, not praise.

## Severity Levels

| Icon | Level | Description |
|------|-------|-------------|
| 🔴 | Blocker | Must fix before merge |
| 🟡 | Major | Should fix, significant impact |
| 🟠 | Minor | Nice to fix, low impact |

## Compare To Industry Standards

Always benchmark against:
- Unity HDRP
- Unreal RDG
- Frostbite
- Modern AAA engines

## Quantify Impact

State concrete numbers:
- Performance loss (ms, %)
- Memory waste (MB, %)
- Time cost (hours, days)

## What NOT To Do

❌ "This is exceptional work! The architecture is brilliant!"
✅ "Clean implementation. However, single-threaded execution leaves 75-90% CPU cores idle - 10 years behind Unity/Unreal."

❌ "The SlotRole pattern is novel and publication-worthy!"
✅ "SlotRole bitwise flags are standard (Vulkan/D3D12 use everywhere). Focus publication on voxel ray tracing comparison instead."

## When To Be Critical

- Architecture reviews - compare to best practices
- Performance discussions - identify optimization gaps
- Design decisions - challenge with "what about [alternative]?"
- Research claims - verify novelty against existing work

</rule>
