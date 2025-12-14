---
tags: [workflow, hacknplan, obsidian, session-management, glue]
created: 2025-12-14
status: active
---

# Session Workflow Integration

Comprehensive workflow for integrating HacknPlan task management with Obsidian documentation via the glue layer.

## Overview

Every development session follows a structured workflow that ensures:
1. **Task tracking** - All work tracked in HacknPlan
2. **Design documentation** - Every task links to architecture docs
3. **Context preservation** - Knowledge captured in Obsidian vault
4. **Cross-domain sync** - Glue layer maintains bidirectional links

---

## Glue Layer - Cross-Domain Cache

**The hacknplan-obsidian-glue MCP acts as the communication/caching layer between HacknPlan and Obsidian.**

### Purpose

The glue layer:
1. **Caches cross-references** - Fast lookup of vault docs for HacknPlan elements and vice versa
2. **Maps folders to element types** - `01-Architecture/` → System (9), `03-Research/` → Mechanic (10)
3. **Translates tags** - Vault tags → HacknPlan tag IDs
4. **Registers new connections** - When new docs/elements are created

### Glue Lookup Flow

```
┌────────────────────────────────────────────────────────────────┐
│                    GLUE LOOKUP FLOW                            │
├────────────────────────────────────────────────────────────────┤
│                                                                │
│  Task Selected → Query Glue for Cross-Refs                     │
│       │                                                        │
│       ├─── Found in Cache ──────────────► Return Links         │
│       │    (fast path)                    - Vault doc paths    │
│       │                                   - Design element ID  │
│       │                                   - Related tags       │
│       │                                                        │
│       └─── Not Found ───────────────────► Search Both Domains  │
│            (slow path)                    │                    │
│                                           ├─ Search HacknPlan  │
│                                           ├─ Search Obsidian   │
│                                           ├─ Register in Glue  │
│                                           └─ Return Links      │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```

### Agent Access to Glue

Both manager agents use glue as the communication layer:

```
┌─────────────────┐                           ┌─────────────────┐
│  HacknPlan MCP  │                           │  Obsidian MCP   │
│  - Work items   │                           │  - Vault docs   │
│  - Design elems │                           │  - Search       │
└────────┬────────┘                           └────────┬────────┘
         │                                             │
         │         ┌─────────────────────┐             │
         └────────►│  Glue MCP (Cache)   │◄────────────┘
                   │  - Cross-references │
                   │  - Folder mappings  │
                   │  - Tag mappings     │
                   └─────────────────────┘
                            │
              ┌─────────────┴─────────────┐
              │                           │
    ┌─────────▼─────────┐       ┌─────────▼─────────┐
    │ hacknplan-manager │       │ obsidian-manager  │
    │ Uses glue to:     │       │ Uses glue to:     │
    │ - Find vault docs │       │ - Find HP elements│
    │ - Map tags        │       │ - Sync new docs   │
    │ - Gen descriptions│       │ - Extract tags    │
    └───────────────────┘       └───────────────────┘
```

### Glue MCP Tools

| Tool | Purpose | When to Use |
|------|---------|-------------|
| `generate_cross_references` | Lookup cached links for doc/element | Task context gathering |
| `map_tags_to_hacknplan` | Translate vault tags to HP tag IDs | Creating work items |
| `sync_vault_to_hacknplan` | Register vault docs as design elements | New vault doc created |
| `sync_hacknplan_to_vault` | Create vault docs for design elements | New design element created |
| `generate_work_item_description` | Create description with vault refs | Creating tasks |
| `scan_vault` | Find vault docs needing sync | Periodic maintenance |
| `extract_vault_tags` | Get all tags from vault | Tag mapping setup |

---

## Session Lifecycle

```
┌─────────────────────────────────────────────────────────────┐
│                    SESSION START                             │
├─────────────────────────────────────────────────────────────┤
│ 1. Memory Bank Check → Quick state understanding            │
│ 2. HacknPlan Check → Sprint tasks, in-progress items        │
│ 3. Task Selection → Move to In Progress + design element    │
│ 4. Glue Lookup → Query cross-refs, register if new          │
│ 5. Context Gather → Design element + Vault + Code refs      │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│                    ACTIVE WORK                               │
├─────────────────────────────────────────────────────────────┤
│ • Every task → Design element + glue registration           │
│ • Unit complete → Commit [HP-id] + Log time + Update vault  │
│ • New docs → Register cross-refs in glue layer              │
│ • Discoveries → Log to HacknPlan + vault + glue             │
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

## Phase 1: Session Start

### 1.1 Memory Bank Quick Check

Read these files for immediate context:
- `memory-bank/activeContext.md` - Current focus, recent changes
- `memory-bank/progress.md` - What's done, what's left

### 1.2 HacknPlan Sprint Check

Query current sprint status via hacknplan-manager:
- Current board tasks
- In-progress tasks (stageId=2)
- Assigned but not started tasks
- Recommended next task based on priority

### 1.3 Task Selection

When user selects a task:
1. Move to In Progress (stageId=2)
2. Set startDate to today
3. Verify design element link exists
4. If no design element → create one + register in glue

### 1.4 Glue Lookup

**Use glue to find cross-references:**
```
Query glue: generate_cross_references(projectId, documentName)
├─ Found → Return vault doc paths, design element ID
└─ Not found → Search both domains → Register → Return
```

### 1.5 Context Gathering

For the selected task, gather context from three sources:
1. **HacknPlan Design Element** → Architecture documentation
2. **Obsidian Vault** → Related vault docs (via glue mapping)
3. **Codebase** → Related source files from task description

---

## Phase 2: Active Work

### 2.1 Unit of Work Completion

After completing each unit of work:

1. **Create Commit** with HacknPlan reference:
   ```
   feat(component): Description [HP-123]
   ```

2. **Log Work Session** via hacknplan-manager

3. **Update Vault** via obsidian-manager:
   - Query glue for related docs
   - Update feature doc status
   - Register any new cross-refs

4. **Update Task Stage** via hacknplan-manager

### 2.2 Glue Registration Protocol

**When creating NEW connections:**

1. **New Design Element** → Register vault mapping
   - hacknplan-manager creates element
   - Calls `sync_hacknplan_to_vault` to create vault doc
   - Glue caches the cross-reference

2. **New Vault Doc** → Register HacknPlan mapping
   - obsidian-manager creates doc
   - Calls `sync_vault_to_hacknplan` to create design element
   - Glue caches the cross-reference

3. **New Work Item** → Use glue for description
   - Use `generate_work_item_description` with vault refs
   - Ensures consistent cross-reference format

---

## Phase 3: Session End

### 3.1 Session Summary
Invoke `session-summary` skill for handoff documentation

### 3.2 HacknPlan Sync
- Log time for all worked tasks
- Add progress comments with commit refs
- Update stages appropriately

### 3.3 Vault Sync (via Glue)
- Query glue for all task-related docs
- Update feature docs
- Create session summary in `Sessions/`
- Register any new cross-references

### 3.4 Commit State
Auto-commit if uncommitted changes exist

---

## Glue Pairing Configuration

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

## Quick Reference

### HacknPlan IDs

| Resource | ID |
|----------|-----|
| Project | 230809 |
| Current Board | 649722 |
| Programming Category | 1 |
| Design Category | 3 |
| Bug Category | 8 |

### Stage IDs

| Stage | ID |
|-------|-----|
| Planned | 1 |
| In Progress | 2 |
| Testing | 3 |
| Completed | 4 |

### Design Element Types

| Type | ID | Use For |
|------|-----|---------|
| System | 9 | Major subsystems |
| Mechanic | 10 | Algorithms, techniques |
| Object | 12 | Data structures |

### Key Vault Paths

| Purpose | Path |
|---------|------|
| Feature plans | `05-Progress/features/` |
| Completed features | `05-Progress/completed/` |
| Session summaries | `Sessions/` |
| Architecture | `01-Architecture/` |
| Research | `03-Research/` |

---

## Related Documentation

- [[HacknPlan-Integration]] - Full HacknPlan API reference
- `.claude/skills/session-setup/SKILL.md` - Session setup skill
- `.claude/skills/collaborative-development/SKILL.md` - Full collaborative workflow
- `.claude/skills/session-summary/skill.md` - Session end workflow

---

*Last updated: 2025-12-14*
