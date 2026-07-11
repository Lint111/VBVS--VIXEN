# Session Management Rules

<rule id="session" reiterate="situational:new-conversation">

## Session Start Protocol (MANDATORY)

New conversation? Execute the **Session Initialization Workflow** in order:

### Phase 1: Memory Bank Quick Check
Read these files to understand current state:
- `memory-bank/activeContext.md` - Current focus, recent changes
- `memory-bank/progress.md` - What's done, what's left

### Phase 2: HacknPlan Sprint Check (MANDATORY)

**Before any work**, delegate to hacknplan-manager:

```
Task(hacknplan-manager, "Session initialization check:
1. List current sprint/board tasks
2. List in-progress tasks (stageId=2)
3. List my assigned tasks not started
4. Return: active work items, current sprint status, recommended next task")
```

**Response includes**:
- Current sprint name and dates
- Active work items (ID, title, stage, estimate)
- Assigned but not started items
- Recommended next task based on priority/dependencies

### Phase 3: Task Selection

When user selects or confirms a task:

1. **Move to In Progress**:
   ```
   Task(hacknplan-manager, "Start task #<id>:
   - Set stageId: 2 (In Progress)
   - Set startDate: today
   - Verify/set design element link
   - Return task details and linked design element")
   ```

2. **If no design element linked**:
   - Search existing design elements for match
   - If none exists, CREATE new design element
   - Link work item to design element
   - **Register in glue layer** (see Phase 4)

### Phase 4: Context Gathering via Glue Layer

**The glue MCP is the communication/caching layer between HacknPlan and Obsidian.**

For the selected task, use glue to find cross-references:

```
Task(obsidian-manager, "Gather context for task #<id> via glue:
1. Query glue for existing cross-references:
   - mcp__hacknplan-obsidian-glue__generate_cross_references(projectId, documentName)
2. If task has design element:
   - Get vault docs mapped to that element type
3. If no glue mapping exists:
   - Search vault for related docs
   - Register new mapping in glue layer
4. Return: consolidated context with all cross-refs")
```

**Glue lookup flow:**
```
Task selected → Query glue for cross-refs →
  ├─ Found → Return cached vault/HacknPlan links
  └─ Not found → Search both domains → Register in glue → Return
```

### Phase 5: Create/Update Feature Doc (if needed)

For new features or architecture changes:
1. Create feature doc in `Vixen-Docs/05-Progress/features/`
2. Link to HacknPlan work item
3. Link to design element
4. **Register cross-reference in glue layer**
5. Add initial requirements and plan

---

## Glue Layer Integration (MANDATORY)

**The hacknplan-obsidian-glue MCP acts as a cache/registry for cross-domain lookups.**

### When to Use Glue

| Operation | Glue Tool | Purpose |
|-----------|-----------|---------|
| Find vault docs for task | `generate_cross_references` | Lookup cached doc links |
| Map vault tags to HP tags | `map_tags_to_hacknplan` | Tag translation |
| New vault doc created | `sync_vault_to_hacknplan` | Register new mapping |
| New design element | `sync_hacknplan_to_vault` | Create vault counterpart |
| Generate task description | `generate_work_item_description` | Include vault refs |

### Glue Registration Protocol

**When creating NEW connections:**

1. **New design element** → Register vault mapping:
   ```
   Task(obsidian-manager, "Register design element in glue:
   - Element ID: <id>
   - Element type: <System|Mechanic|Object>
   - Create/find vault doc in mapped folder
   - Update glue pairing if new folder mapping needed")
   ```

2. **New vault doc** → Register HacknPlan mapping:
   ```
   Task(obsidian-manager, "Register vault doc in glue:
   - Doc path: <vault path>
   - Extract tags → map to HacknPlan tags
   - Find/create linked design element
   - Register cross-reference")
   ```

3. **New work item with vault refs** → Use glue to generate description:
   ```
   Task(hacknplan-manager, "Create work item with vault refs:
   - Use generate_work_item_description to include vault cross-refs
   - Ensures consistent linking format")
   ```

### Glue Pairing Configuration

Current pairing (projectId: 230809):

| Vault Folder | Design Element Type |
|--------------|---------------------|
| 01-Architecture/ | System (9) |
| 03-Research/ | Mechanic (10) |

| Vault Tag | HacknPlan Tag ID |
|-----------|------------------|
| vulkan | 1 |
| render-graph | 2 |
| svo | 3 |
| ray-tracing | 4 |
| shader | 5 |
| documentation | 6 |
| refactor | 7 |
| performance | 8 |

---

## Design Element Requirements (MANDATORY)

**Every task MUST have a linked design element.**

### Design Element Types

| Type ID | Name | Use For |
|---------|------|---------|
| 9 | System | Major subsystems (RenderGraph, SVO, Profiler) |
| 10 | Mechanic | Algorithms, techniques (ESVO, beam optimization) |
| 12 | Object | Data structures, resources |
| 13 | Folder | Organization/grouping |

### Creating Design Elements (with Glue Registration)

When a task needs a new design element:

```
Task(hacknplan-manager, "Create design element with glue registration:
- Type: <9|10|12> based on task type
- Name: <descriptive name>
- Description: <markdown description with vault refs>
- Link to work item: #<id>
- After creation: register in glue layer for vault sync")
```

---

## MCP Delegation (MANDATORY)

**DO NOT use MCP tools directly in main conversation.** Delegate to specialized agents:

| MCP Tool Prefix | Delegate To | Model | Role |
|-----------------|-------------|-------|------|
| `mcp__hacknplan__*` | `hacknplan-manager` | Haiku | Task/element management |
| `mcp__obsidian-vault__*` | `obsidian-manager` | Haiku | Vault operations |
| `mcp__hacknplan-obsidian-glue__*` | EITHER agent | Haiku | Cross-domain cache |

**Glue access:** Both agents can use glue MCP as the communication layer:
- hacknplan-manager: Uses glue to find vault docs for tasks
- obsidian-manager: Uses glue to find HacknPlan elements for vault docs

**Why**: MCP tool definitions bloat context. Delegation keeps main conversation focused.

---

## Unit of Work Completion Protocol

After completing a unit of work (feature, bug fix, subtask):

### 1. Create Commit
Standard git commit with HacknPlan reference:
```
feat(component): Description [HP-<id>]
```

### 2. Log Work Session
```
Task(hacknplan-manager, "Log work completion for #<id>:
- Hours: <actual time>
- Summary: <what was done>
- Commit: <hash>
- Files: <changed files>")
```

### 3. Update Vault (via Glue)
```
Task(obsidian-manager, "Update docs for completed work:
- Query glue for related vault docs
- Update feature doc status
- Add progress log entry
- Update any affected architecture docs
- Register any new cross-references in glue")
```

### 4. Update Task Stage
```
Task(hacknplan-manager, "Update task #<id>:
- If testing needed: stageId=3
- If complete: stageId=4, isCompleted=true")
```

---

## Session End Protocol

When ending a session (user says "done", "wrap up", "summarize"):

### 1. Invoke session-summary skill
Generates comprehensive handoff documentation

### 2. HacknPlan Sync
- Log time for all worked tasks
- Add progress comments
- Update stages appropriately

### 3. Vault Sync (via Glue)
- Query glue for all task-related docs
- Update feature docs
- Create session summary in `Vixen-Docs/Sessions/`
- Update `activeContext.md`
- Register any new cross-references

### 4. Commit Session State
Auto-commit if uncommitted changes exist

---

## Context Window Management

### Enable Extended Context
For detailed task work, use extended context capabilities:
- Load full design element documentation
- Load related vault docs completely
- Keep task context throughout session

### Context Preservation
- Summarize frequently to preserve key decisions
- Update vault docs with discoveries
- Log insights to HacknPlan comments
- **Register discoveries in glue for future lookups**

---

## Full Context (After Reset)

When starting fresh or after conversation compression:

1. `memory-bank/projectbrief.md` - Goals, scope
2. `memory-bank/productContext.md` - Design philosophy
3. `memory-bank/systemPatterns.md` - Architecture patterns
4. `memory-bank/techContext.md` - Tech stack
5. `memory-bank/activeContext.md` - Current focus
6. `memory-bank/progress.md` - Status

---

## Sections to NEVER Delete in activeContext.md

These provide essential long-term context:

- **Week 1 & 1.5+ Success Criteria** - project milestones
- **Known Limitations** - accepted edge cases
- **Reference Sources** - ESVO paths, paper citations
- **Todo List (Active Tasks)** - task tracking across weeks

---

## Quick Reference: Session Lifecycle

```
┌─────────────────────────────────────────────────────────────┐
│                    SESSION START                             │
├─────────────────────────────────────────────────────────────┤
│ 1. Memory Bank Check → Quick state understanding            │
│ 2. HacknPlan Check → Sprint tasks, in-progress items        │
│ 3. Task Selection → Move to In Progress + design element    │
│ 4. Glue Lookup → Query cross-refs, register if new          │
│ 5. Context Gather → Design element + Vault + Code refs      │
│ 6. Feature Doc → Create/update + register in glue           │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│                    ACTIVE WORK                               │
├─────────────────────────────────────────────────────────────┤
│ • Every task → Design element + glue registration           │
│ • Unit complete → Commit [HP-id] + Log time + Update vault  │
│ • New docs → Register cross-refs in glue layer              │
│ • Discoveries → Log to HacknPlan comments + vault + glue    │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│                    SESSION END                               │
├─────────────────────────────────────────────────────────────┤
│ 1. Session Summary → Handoff documentation                  │
│ 2. HacknPlan Sync → Log time, comments, stage updates       │
│ 3. Vault Sync → Feature docs, session archive (via glue)    │
│ 4. Glue Sync → Ensure all new cross-refs registered         │
│ 5. Commit State → Auto-commit if changes exist              │
└─────────────────────────────────────────────────────────────┘
```

---

## Glue Layer Architecture

```
┌─────────────────┐                           ┌─────────────────┐
│  HacknPlan MCP  │                           │  Obsidian MCP   │
│  - Work items   │                           │  - Vault docs   │
│  - Design elems │                           │  - Search       │
│  - Tags/stages  │                           │  - Create/edit  │
└────────┬────────┘                           └────────┬────────┘
         │                                             │
         │         ┌─────────────────────┐             │
         └────────►│  Glue MCP (Cache)   │◄────────────┘
                   │  - Cross-references │
                   │  - Folder mappings  │
                   │  - Tag mappings     │
                   │  - Bidirectional    │
                   └─────────────────────┘
                            │
              ┌─────────────┴─────────────┐
              │                           │
    ┌─────────▼─────────┐       ┌─────────▼─────────┐
    │ hacknplan-manager │       │ obsidian-manager  │
    │ (uses glue to     │       │ (uses glue to     │
    │  find vault docs) │       │  find HP elements)│
    └───────────────────┘       └───────────────────┘
```

</rule>