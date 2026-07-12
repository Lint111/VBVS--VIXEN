# Agent Selection Rules

<rule id="agents" reiterate="task-relevant:agent-launch">

## NEVER use passive/background waits — active polling only, no exceptions

**The failure pattern this rule exists to stop:** an agent polls a shell command correctly 2-3
times, then talks itself into "I should stop doing no-op queries and use a background
mechanism to wake me up instead" — and switches to `ScheduleWakeup`, `Monitor`, a background
task, or a self-invented shell trick (`sleep N && <signal>`, a detached watcher process, a
cron-style callback). That switch is the bug. This machine has repeatedly, observably failed
to reliably resume an agent parked on a passive/background wait — the agent goes idle waiting
for a callback that may never fire, and the whole turn silently stalls with no one watching.
This has happened multiple times across this project's history despite repeated correction.

**The rule, unconditionally:** any wait for a long-running external process (build, configure,
render, deploy, queue position, or any other multi-minute operation) MUST be an ACTIVE
FOREGROUND polling loop — a real command (or the harness's Monitor/until-loop equivalent used
in its FOREGROUND, blocking form) that re-checks status on a ~15-30s interval and prints a
readable status line each iteration, keeping both the user informed and the agent's own process
genuinely alive and attentive. Never hand the wait off to something that returns control and
expects to be called back later. If you find yourself thinking "I've checked this enough times
manually, let me set up something to notify me instead" — that thought is the trigger to STOP
and keep polling, not to switch mechanisms. There is no volume of prior manual polling that
makes a background/passive wait safe on this machine. Do not rationalize an exception.

This applies to every subagent this rule reaches (via `reiterate="task-relevant:agent-launch"`)
and every worker dispatched through the `post-brainstorm-context-manager` skill — restate it
explicitly in every implementer/validator brief that involves a build, queue, or other
long-running wait, do not assume the worker already internalized it from this file alone.

## Available Agents

| Agent | Use For | Model |
|-------|---------|-------|
| `coding-partner` | **Default** - all dev work | Opus |
| `bug-hunter` | Persistent/complex bugs | Opus |
| `architecture-critic` | After major changes | Opus |
| `vulkan-expert` | Vulkan issues, validation errors | Opus |
| `shader-debugger` | GLSL/SPIR-V problems | Opus |
| `data-scientist` | Data analysis, trends, visualization | Opus |
| `ui-ux-engineer` | UI/UX design, DX optimization, accessibility | Opus |
| `hacknplan-manager` | HacknPlan tasks, sprints, time logging | Sonnet |
| `obsidian-manager` | Vault docs, search, cross-references | Sonnet |
| `word-manager` | Word documents, reports, PDF export | Sonnet |
| `excel-manager` | Excel spreadsheets, data tables, formatting | Sonnet |
| `unity-manager` | Unity Editor operations, scripts, tests | Sonnet |
| `intern-army-refactor` | Codebase-wide changes | Haiku |

## MCP Delegation Agents

These agents handle MCP tools to reduce main conversation context load:

### hacknplan-manager (Sonnet)
**Exclusive access to**: All `mcp__hacknplan__*` tools

**Delegate when**:
- Creating/updating/querying work items
- Managing sprints, boards, milestones
- Logging work sessions and time
- Updating task status
- Breaking features into tracked subtasks

**Example triggers**:
- "Create a task for..."
- "Log 2 hours on..."
- "What's in the current sprint?"
- "Mark task X as complete"

### obsidian-manager (Sonnet)
**Exclusive access to**: All `mcp__obsidian-vault__*` and `mcp__hacknplan-obsidian-glue__*` tools

**Delegate when**:
- Creating/updating vault documentation
- Searching vault for information
- Generating cross-references
- Managing session notes
- Syncing vault with HacknPlan

**Example triggers**:
- "Document the new feature"
- "Search vault for ESVO"
- "Update architecture docs"
- "Generate session summary"

### word-manager (Sonnet)
**Exclusive access to**: All `mcp__word__*` tools

**Delegate when**:
- Creating/editing Word documents
- Generating formatted reports
- Converting documents to PDF
- Adding tables, charts, or structured content
- Exporting documentation for distribution

**Example triggers**:
- "Create a Word report"
- "Export benchmark results to docx"
- "Convert the doc to PDF"
- "Add a formatted table to the report"

### excel-manager (Sonnet)
**Exclusive access to**: All `mcp__excel__*` tools

**Delegate when**:
- Reading/writing Excel spreadsheets
- Creating formatted data tables
- Applying cell formatting and styles
- Aggregating data from multiple sources to Excel
- Creating workbook reports with multiple sheets

**Example triggers**:
- "Read the benchmark Excel file"
- "Create an Excel report from the JSON data"
- "Format the spreadsheet with headers"
- "Add a summary sheet to the workbook"

**Note**: For data analysis and visualization, use `data-scientist` instead.
`excel-manager` focuses on Excel file manipulation; `data-scientist` handles statistics and charts.

### unity-manager (Sonnet)
**Exclusive access to**: All `mcp__UnityMCP__*` tools

**Delegate when**:
- Creating/modifying GameObjects, components, prefabs
- Editing C# scripts without triggering domain reload
- Running Unity tests
- Managing scenes and assets
- Executing Unity Editor menu commands

**Example triggers**:
- "Create a test scene"
- "Add a component to the player"
- "Run the visibility tests"
- "Create a prefab from this object"

## Agent Descriptions

### coding-partner (DEFAULT)
- Use for: Feature implementation, bug fixes, refactoring, architecture discussions
- Behavior: Delegates to specialized agents automatically
- Use this unless you have a specific reason not to

### bug-hunter
- Trigger: Persistent bugs after multiple fix attempts
- Trigger: User says "I've tried...", "still not working"
- Systematically traces through code to find root cause

### architecture-critic
- Trigger: After significant code changes or refactors
- Provides critical analysis against industry standards
- Identifies technical debt and improvement areas

### vulkan-expert
- Trigger: Validation layer errors, pipeline issues
- Consults Vulkan spec and best practices
- Handles synchronization, descriptor sets, memory

### shader-debugger
- Trigger: GLSL compilation errors, visual artifacts
- Handles compute, fragment, vertex shaders
- SVO traversal, ray marching bugs

### data-scientist
- Trigger: Analyzing benchmark results, performance metrics
- Trigger: Creating visualizations or trend analysis
- Trigger: Validating hypotheses with collected data
- Aggregates data, calculates statistics, identifies patterns
- Creates comprehensive reports with clear visuals
- Recommends additional data points to collect

### ui-ux-engineer
- Trigger: Interface design or usability analysis needed
- Trigger: API ergonomics or developer experience issues
- Trigger: Accessibility review or compliance check
- Trigger: CLI design or error message improvement
- Designs user interfaces and interaction flows
- Evaluates developer experience (API, tooling, docs)
- Conducts UX audits and accessibility reviews
- Creates design specifications and component libraries

### intern-army-refactor
- Trigger: Systematic changes across entire codebase
- Examples: Rename class, add method to all implementations
- Uses Haiku for fast parallel processing

## User-Invocable Skills

Skills are invoked with the `Skill` tool or `/skill-name` slash command. They execute in the main conversation context.

### pre-commit-review
**Invocation**: `/pre-commit-review` or `Skill("pre-commit-review")`

**Trigger**:
- Before creating any git commit
- User asks for "code review", "review changes", "check my code"
- After implementing features to catch quality issues
- Before creating pull requests

**Purpose**:
- Harsh senior developer code review before commit
- Catches lazy solutions, bad code smells, security risks
- Identifies files that should be in .gitignore
- Finds temporary files, cache files, local configs
- Checks for hardcoded secrets and credentials
- Analyzes edge cases and missing error handling
- Compares against industry standards

**Output**:
- Severity-based report (🔴 Blocker, 🟡 Major, 🟠 Minor)
- Specific file:line references for each issue
- Recommended fixes and .gitignore updates
- Blocks commit if critical issues found

**Example triggers**:
- "Review my changes before committing"
- "Check if I'm about to commit anything bad"
- "Run pre-commit review"

### debugging-known-issues
**Invocation**: `/debugging-known-issues` or `Skill("debugging-known-issues")`

**Trigger**:
- Encountering runtime errors or crashes
- Validation layer errors
- Mysterious null handles or VK_NULL_HANDLE issues
- Before deep-diving into a bug

**Purpose**:
- Quick reference for known VIXEN debugging patterns
- Common issues catalog with diagnosis steps
- Issue-specific fix patterns

### gemini-codebase-analysis
**Invocation**: `/gemini-codebase-analysis` or `Skill("gemini-codebase-analysis")`

**Trigger**:
- Analyzing entire codebase (exceeds Claude context)
- Verifying implementations across many files
- Checking for patterns/security across large codebases
- When data exceeds Claude's context limits

**Purpose**:
- Leverages Gemini's 2M+ token context window
- Whole-codebase analysis in single context
- Security audits and pattern verification

### hacknplan-docs / obsidian-docs
**Invocation**: `/hacknplan-docs` or `/obsidian-docs`

**Purpose**:
- Quick documentation for HacknPlan/Obsidian MCP usage
- Should delegate to hacknplan-manager/obsidian-manager agents instead

### project-rules
**Invocation**: `Skill("project-rules")` (auto-invoked by hook)

**Purpose**:
- Loads behavioral rules for current session
- Communication, engineering, obsidian-first, logging rules
- Task-specific rules (code review, workflow, agents)

**Trigger**: Automatically invoked at start of every response via UserPromptSubmit hook

### session-summary
**Invocation**: `/session-summary` or `Skill("session-summary")`

**Trigger**:
- End of work session
- User says "summary", "handoff", "wrap up", "I'm done for today"
- Before switching tasks or branches

**Purpose**:
- Generates comprehensive handoff documentation
- Captures file changes, decisions, insights, next steps
- Updates Obsidian vault and HacknPlan with progress
- Auto-commits changes with detailed message

### session-setup
**Invocation**: `/session-setup` or `Skill("session-setup")`

**Trigger**:
- Every new conversation (can be auto-invoked)
- User says "start session", "new session", "what should I work on?"
- After conversation compression/reset
- When switching between tasks

**Purpose**:
- Standardized session initialization
- HacknPlan sprint check and task selection
- Design element verification
- Obsidian context gathering
- Sets up working context for the session

### collaborative-development
**Invocation**: `/collaborative-development` or `Skill("collaborative-development")`

**Trigger**:
- Starting a new feature implementation
- Tackling complex problems with multiple components
- Tasks requiring architectural decisions
- Work spanning multiple files/subsystems

**Purpose**:
- Multi-agent collaborative workflow
- Orchestrates planning, implementation, peer review
- HacknPlan/Obsidian integration with design element linking
- Iterative refinement with user approval

### red-green-test-cycle
**Invocation**: `/red-green-test-cycle` or `Skill("red-green-test-cycle")`

**Trigger**:
- Fixing failing tests systematically
- Working through test suite one test at a time
- Validating test relevance
- Adding test metadata

**Purpose**:
- Test-driven debugging workflow
- Red (failing) → Yellow (review) → Green (passing) cycle
- Minimal intrusive builds
- Systematic test fixing with validation

### gpu-shader-debug
**Invocation**: `/gpu-shader-debug` or `Skill("gpu-shader-debug")`

**Trigger**:
- Debugging GLSL shaders (compute/fragment/vertex)
- Ray marching issues or visual artifacts
- Voxel traversal bugs
- Any GPU-side rendering problems

**Purpose**:
- Creates 1:1 C++ mirror of GLSL shader
- Enables CPU-side step-through debugging
- Unit testing for shader logic
- Systematic bug fixing then port back to GLSL

</rule>
