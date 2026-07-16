# Workflow Rules

<rule id="workflow" reiterate="task-relevant:coding,agent-launch">

## Default Agent

Use `coding-partner` agent for all development work.
It delegates to specialized agents automatically.

## Preferred Execution Path for Plans

Once a plan exists and is approved, prefer executing it through the
`post-brainstorm-context-manager` skill rather than running milestones inline in the main
conversation. It chunks the plan into milestones, dispatches fresh model-tiered workers per
milestone, validates with Opus, and persists progress to the plan doc — keeping the controller's
context thin. Default to it for any multi-milestone plan; skip it only for work small enough that
the overhead isn't worth it (a single-file fix, no distinct milestones).

## File Paths

**Always use absolute Windows paths with drive letters:**

✅ `C:\cpp\VBVS--VIXEN\VIXEN\libraries\Profiler\src\BenchmarkConfig.cpp`
❌ `libraries/Profiler/src/BenchmarkConfig.cpp`
❌ `./libraries/Profiler/src/BenchmarkConfig.cpp`

## Agent Protocol

**Before launching agent:**
- State which agent and why
- Example: "Launching `bug-hunter` agent to investigate the crash..."

**After agent completes:**
- Summarize findings/actions
- Provide file:line references
- Example: "Fixed null check in [Renderer.cpp:156](Renderer.cpp#L156)"

## Session Updates

After significant work, update `memory-bank/activeContext.md` with:
- What was completed
- Test results
- Files modified (with line numbers)
- Next steps
- Any blockers

</rule>
