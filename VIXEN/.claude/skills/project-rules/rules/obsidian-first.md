# Obsidian-First Documentation Rule

<rule id="obsidian-first" reiterate="always">

## Core Principle

Obsidian vault (`Vixen-Docs/`) is the **primary documentation source and output**.

## Quick Lookup Index

For fast retrieval, check these locations FIRST by topic:

| Topic | Primary File | Fallback Search |
|-------|--------------|-----------------|
| RenderGraph nodes | `01-Architecture/RenderGraph-System.md` | `Libraries/RenderGraph.md` |
| Node creation | `templates/Node-Documentation.md` | grep "NodeInstance" |
| Slot system | `01-Architecture/RenderGraph-System.md#5` | grep "SlotRole" |
| Vulkan pipeline | `01-Architecture/Vulkan-Pipeline.md` | grep "VkPipeline" |
| SVO/Voxels | `03-Research/` | grep "ESVO\|voxel" |
| Logging | `04-Development/` | grep "Logger" |
| Build commands | `CLAUDE.md` | `rules/commands.md` |
| Session context | `memory-bank/activeContext.md` | `05-Progress/` |

## Efficient Lookup Order

### Step 0: Quick Lookup Index (FASTEST)
```
Read: Vixen-Docs/00-Index/Quick-Lookup.md
```
This master index has direct links to all topics. **Check here FIRST.**

### Step 1: Direct File Access
```
Read: Vixen-Docs/{path-from-index}.md
```

### Step 2: Grep Search (if not in Quick-Lookup)
```
Grep: pattern in Vixen-Docs/ --head_limit 5
```

### Step 3: Codebase Fallback
```
Grep: pattern in libraries/ or source/
```

### Step 4: Document Findings
```
Write: Create/update Obsidian doc for future lookups
```

## Vault Structure

```
Vixen-Docs/
├── 00-Index/        # Navigation, quick-reference
├── 01-Architecture/ # System design, patterns ← START HERE
├── 02-Implementation/ # How-to guides
├── 03-Research/     # Papers, algorithms
├── 04-Development/  # Logging, debugging
├── 05-Progress/     # Session notes
├── Libraries/       # Per-library docs
└── templates/       # Doc templates ← FOR NEW DOCS
```

## Topic-to-File Mapping

### Architecture Questions
- "How does X work?" → `01-Architecture/`
- "What is the design of Y?" → `01-Architecture/Overview.md`

### Implementation Questions
- "How do I create a node?" → `templates/Node-Documentation.md`
- "How do I add logging?" → `04-Development/`
- "How do I build?" → `rules/commands.md`

### Research Questions
- "What algorithm for X?" → `03-Research/`
- "ESVO traversal?" → `03-Research/` + grep

## File Naming

- **Concepts**: PascalCase (`RenderGraphNodes.md`)
- **How-tos**: kebab-case (`how-to-add-logging.md`)

## Update Trigger

When knowledge gap found → create/update Obsidian doc:
1. Use `templates/` for structure
2. Place in appropriate folder
3. Add frontmatter with tags
4. Link to related docs with `[[wikilinks]]`

</rule>
