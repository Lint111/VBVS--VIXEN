# HacknPlan Workflow Rules

<rule id="hacknplan-workflow" reiterate="always">

## Task-Driven Development

All work MUST be tracked in HacknPlan. No untracked work.

### Before Starting Any Work

1. **Check existing tasks** via hacknplan-manager agent
2. **If task exists**: Move to In Progress, link your work
3. **If no task**: Create one with full metadata first

### Task Lifecycle (MANDATORY)

```
[Create] → [Planned] → [In Progress] → [Testing] → [Completed]
    ↓          ↓             ↓              ↓            ↓
 metadata   stage=1      stage=2        stage=3      stage=4
 assigned   dates set    work begins    validating   isCompleted
 estimated  linked       logging time   tests run    time logged
```

### Required Metadata on Create (v7.0.0)

| Field | Required | Default | Example |
|-------|----------|---------|---------|
| title | ✅ | - | `[RenderGraph] Add slot validation` |
| categoryId | ✅ | - | 1 (Programming) or "programming" |
| isStory | Auto | false | false (regular task) |
| importanceLevelId | Auto | 1 | 3 (Normal) or "normal" |
| estimatedCost | Auto | 0 | 4 (hours) |
| description | ⚠️ | "" | See template |
| tagIds | ✅ | - | `[1, 2]` or `["vulkan", "performance"]` |
| assignedUserIds | ✅ | - | `[230909]` - works on CREATE! |
| boardId | ⚠️ | backlog | 649722 (current sprint) |
| designElementId | ⚠️ | null | Link to arch doc |

**v7.0.0 Features:**
- Name resolution for categories, importance levels, tags
- Batch operations for creating/updating multiple items
- Atomic creates with automatic rollback on failure
- Template system for common work item patterns

### Stage Updates

Update stage when status changes:
- Starting work → `stageId: 2`
- Code complete → `stageId: 3`
- Tests pass → `stageId: 4, isCompleted: true`

### Time Logging

Log time after EVERY work session:
```
log_work_session(hours, description, date)
```

### Design Element Linking

For architecture/research tasks, link to design elements that map to vault docs.

### Delegation Rule

**NEVER call HacknPlan MCP tools directly.** Always delegate:
```
Task(hacknplan-manager, "Create task for [Component] Feature with 4h estimate")
```

### Advanced Operations (v7.0.0)

For batch operations, use specialized tools:
- Creating 5+ tasks → `batch_create_work_items`
- Updating multiple tasks → `batch_update_work_items`
- Tagging multiple items → `batch_assign_tags`
- Logging time across tasks → `batch_log_work`

For atomic operations with rollback:
- `create_work_item_atomic` - ensures all metadata is set or nothing changes

For templates:
- `list_templates` - see available templates
- `create_from_template` - create with placeholder expansion

</rule>
