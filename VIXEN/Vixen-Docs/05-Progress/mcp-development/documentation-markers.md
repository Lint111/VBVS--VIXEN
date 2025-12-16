# Documentation Marker System

**Created**: 2025-12-16
**Status**: Design Phase
**Tasks**: HP-32, HP-33
**Related**: [glue-sync-engine.md](glue-sync-engine.md)

## Overview

The Documentation Marker System creates bidirectional synchronization between Obsidian vault documents and HacknPlan work items through special markers embedded in markdown files.

**Two-Way Flow:**
```
Vault Markers → HacknPlan Tickets  (HP-32)
HacknPlan Events → Vault Markers   (HP-33)
```

---

## Part 1: Vault → HacknPlan Markers (HP-32)

### Purpose
Allow developers to create HacknPlan work items directly from documentation using markdown markers.

### Workflow
```
1. Developer writes doc with #Todo marker
2. Run process_vault_markers tool
3. Parser extracts marker metadata
4. Creates HacknPlan work item
5. Updates vault with hacknplan_id
6. Links ticket to document via design element
```

---

## Marker Type Reference

### 1. #Todo Marker

**Creates**: Standard work item in HacknPlan

**Syntax:**
```markdown
#Todo[{category}|{estimate}h|{tags}]: Description text
```

**Parameters:**
- `{category}` - programming | design | bug | documentation | research (required)
- `{estimate}` - Hours estimate, e.g., `2h`, `0.5h` (optional)
- `{tags}` - Comma-separated tags, e.g., `feature,critical` (optional)
- Description - Ticket title/summary (required)

**Examples:**

```markdown
# Phase 8 Implementation

#Todo[programming|6h|testing,optimization]: Implement single-file sync optimization
#Todo[programming|2h|performance]: Add LRU cache for vault scan results
#Todo[bug|1h|critical]: Fix memory leak in sync queue processing
#Todo[documentation|1h]: Update architecture diagrams for Phase 7
#Todo[design|4h|ux]: Create dashboard mockups for queue stats
```

**Generated Work Item:**
- **Title**: "Implement single-file sync optimization"
- **Category**: Programming
- **Estimated Cost**: 6 hours
- **Tags**: testing, optimization
- **Description**: Link to source document
- **Design Element**: Linked to vault document

---

### 2. #Feature Marker

**Creates**: Feature request work item with milestone tracking

**Syntax:**
```markdown
#Feature[{priority}|{milestone}]: Feature description
```

**Parameters:**
- `{priority}` - urgent | high | normal | low (optional, defaults to normal)
- `{milestone}` - Milestone name or ID (optional)
- Description - Feature title (required)

**Examples:**

```markdown
# Future Enhancements

#Feature[high|v2.0]: Real-time collaboration with multi-user sync
#Feature[normal|Phase 9]: Export vault docs to PDF with attachments
#Feature[urgent]: Add conflict resolution UI for manual review
#Feature: Support for nested vault folders (defaults to normal priority)
```

**Generated Work Item:**
- **Category**: Ideas (or feature category if configured)
- **Importance Level**: Maps from priority
- **Milestone**: Associated milestone
- **Description**: Feature details + source doc link

---

### 3. #Limitation Marker

**Creates**: Documentation work item to address limitation

**Syntax:**
```markdown
#Limitation[{severity}|{target}]: Description of limitation
```

**Parameters:**
- `{severity}` - known | blocking | workaround | wontfix (optional)
- `{target}` - Phase/version to address, e.g., `Phase 8`, `v2.0` (optional)
- Description - What the limitation is (required)

**Examples:**

```markdown
# Known Limitations

#Limitation[known|Phase 8]: Full vault sync on every change (inefficient for large vaults)
#Limitation[blocking|v1.1]: No bidirectional sync HacknPlan → Vault yet
#Limitation[workaround]: Windows path handling requires forward slashes in config
#Limitation[wontfix]: Cannot sync binary attachments (by design)
```

**Behavior:**
- `known` - Creates low-priority task
- `blocking` - Creates high-priority task
- `workaround` - Adds to documentation, no task
- `wontfix` - Documentation only, no task

---

### 4. #Bug Marker

**Creates**: Bug work item in HacknPlan

**Syntax:**
```markdown
#Bug[{severity}|{tags}]: Bug description
```

**Parameters:**
- `{severity}` - critical | high | normal | minor (required)
- `{tags}` - Related tags, e.g., `regression,performance` (optional)
- Description - Bug summary (required)

**Examples:**

```markdown
# Known Issues

#Bug[critical|regression]: Sync fails on files >10MB due to timeout
#Bug[high|performance]: Queue processing slows down after 100+ items
#Bug[normal|ux]: Progress indicator not updating during long syncs
#Bug[minor]: Typo in error message for missing API key
```

**Generated Work Item:**
- **Category**: Bug
- **Importance Level**: Maps from severity (critical→urgent, etc.)
- **Tags**: From marker + auto-add "from-vault"
- **Description**: Bug details + reproduction steps from doc

---

### 5. #Question Marker (Documentation Only)

**Does NOT create ticket** - For internal documentation questions

**Syntax:**
```markdown
#Question: What is the best approach for...?
```

**Example:**
```markdown
# Design Decisions

#Question: Should we use LRU cache or Redis for sync state?
#Question: What's the optimal debounce delay for file watcher?
```

**Behavior:**
- Searchable in vault
- Helps track unresolved design questions
- Can be answered inline with `#Answer:` marker

---

## Marker Parsing Rules

### Location
Markers can appear anywhere in document:
- ✅ After headings
- ✅ In lists
- ✅ In blockquotes
- ✅ Inline in paragraphs
- ❌ NOT inside code blocks
- ❌ NOT inside frontmatter

### Processing
1. **Scan vault** for .md files
2. **Parse each line** for marker patterns
3. **Extract metadata** using regex
4. **Validate parameters** (category exists, valid tags, etc.)
5. **Generate operation** (CreateOperation)
6. **Execute batch** via existing sync system
7. **Update vault** with ticket IDs

### Deduplication
- If marker already has `hacknplan_id` in comment, skip
- Example: `#Todo[programming|2h]: Task <!-- HP-45 -->`

---

## Part 2: HacknPlan → Vault Markers (HP-33)

### Purpose
Notify vault documents when related HacknPlan work items change state, requiring review.

### Workflow
```
1. HacknPlan work item moves to "Completed"
2. Find linked vault documents (via design element)
3. Inject #NeedsReview marker into document
4. Developer runs review_vault_markers tool
5. Reviews documentation updates
6. Runs clear_vault_marker after update
```

---

## Review Marker Types

### 1. #NeedsReview Marker

**Trigger**: Work item moved to "Completed" or "In Review"

**Syntax:**
```html
<!-- #NeedsReview[HP-{id}|{date}]: {reason} -->
```

**Injection Location**: After frontmatter, before first heading

**Examples:**
```markdown
---
title: Sync Queue Architecture
hacknplan_id: 16
---

<!-- #NeedsReview[HP-16|2025-12-16]: Sync Queue implementation complete - update diagrams -->

# Sync Queue Architecture
...
```

**Review Checklist**:
- [ ] Code examples updated?
- [ ] Architecture diagrams current?
- [ ] Performance metrics accurate?
- [ ] API reference up-to-date?

---

### 2. #OutOfSync Marker

**Trigger**: Work item description updated but vault document unchanged for >7 days

**Syntax:**
```html
<!-- #OutOfSync[HP-{id}|{lastSync}]: Doc may be stale, HacknPlan updated {date} -->
```

**Example:**
```html
<!-- #OutOfSync[HP-15|2025-12-09]: Doc may be stale, HacknPlan updated 2025-12-16 -->
```

**Action**: Review HacknPlan work item for changes, update vault doc

---

### 3. #Completed Marker

**Trigger**: Work item marked complete (informational only)

**Syntax:**
```html
<!-- #Completed[HP-{id}|{date}]: {title} -->
```

**Injection Location**: Near related content or end of document

**Example:**
```markdown
## File Watcher Implementation

<!-- #Completed[HP-15|2025-12-16]: File Watcher with chokidar and debouncing -->

The file watcher monitors vault changes in real-time...
```

---

## MCP Tools

### Vault → HacknPlan Tools

#### `process_vault_markers`
**Description**: Scan vault for markers and create/update HacknPlan work items

**Arguments:**
```typescript
{
  projectId: number;          // HacknPlan project ID
  folder?: string;            // Specific folder to scan (optional)
  markerTypes?: string[];     // Filter: ['todo', 'bug', 'feature'] (optional)
  dryRun?: boolean;           // Preview without creating tickets
}
```

**Returns:**
```typescript
{
  scanned: number;            // Documents scanned
  markersFound: number;       // Total markers found
  created: number;            // Tickets created
  updated: number;            // Tickets updated
  errors: Array<{path, marker, error}>;
  preview?: Array<{marker, wouldCreate}>; // If dryRun
}
```

**Example:**
```typescript
await process_vault_markers({
  projectId: 230955,
  folder: '05-Progress/mcp-development',
  markerTypes: ['todo', 'bug'],
  dryRun: false
});
```

---

#### `clear_processed_markers`
**Description**: Remove or mark markers that have been processed

**Arguments:**
```typescript
{
  projectId: number;
  filePath: string;           // Specific file
  markerIds?: string[];       // Specific markers to clear (optional)
}
```

**Behavior:**
- Converts `#Todo[...]` to `#Todo[...] <!-- HP-45 -->`
- Prevents re-processing on next scan

---

### HacknPlan → Vault Tools

#### `review_vault_markers`
**Description**: List all review markers in vault

**Arguments:**
```typescript
{
  projectId: number;
  markerTypes?: string[];     // Filter: ['needsreview', 'outofsync']
  sortBy?: 'date' | 'priority';
}
```

**Returns:**
```typescript
{
  markers: Array<{
    type: 'needsreview' | 'outofsync' | 'completed';
    filePath: string;
    workItemId: number;
    date: string;
    reason: string;
    line: number;
  }>;
  total: number;
}
```

---

#### `clear_vault_marker`
**Description**: Remove a review marker after documentation update

**Arguments:**
```typescript
{
  projectId: number;
  filePath: string;
  workItemId: number;         // Which marker to remove
}
```

**Behavior:**
- Removes marker from document
- Logs clearance in sync state
- Optionally adds `#Reviewed[HP-{id}|{date}]` marker

---

#### `inject_review_markers`
**Description**: Manually inject markers for testing or bulk updates

**Arguments:**
```typescript
{
  projectId: number;
  workItemIds: number[];      // Work items to create markers for
  markerType: 'needsreview' | 'outofsync';
  reason?: string;
}
```

---

## Implementation Details

### Phase 1: Parser (2h)

**File**: `src/lib/marker-parser.ts`

```typescript
export interface ParsedMarker {
  type: 'todo' | 'feature' | 'bug' | 'limitation' | 'question';
  category?: string;
  estimate?: number;
  tags?: string[];
  priority?: string;
  severity?: string;
  milestone?: string;
  description: string;
  line: number;
  raw: string;
  alreadyProcessed: boolean; // Has <!-- HP-{id} -->
}

export function parseMarkersInDocument(
  content: string,
  filePath: string
): ParsedMarker[]
```

**Regex Patterns:**
```typescript
const TODO_PATTERN = /#Todo\[([^\]]+)\]:\s*(.+?)(?:<!--\s*HP-(\d+)\s*-->)?$/;
const FEATURE_PATTERN = /#Feature\[([^\]]+)\]:\s*(.+)/;
const BUG_PATTERN = /#Bug\[([^\]]+)\]:\s*(.+)/;
const LIMITATION_PATTERN = /#Limitation\[([^\]]+)\]:\s*(.+)/;
```

---

### Phase 2: Operation Generator (1h)

**File**: `src/lib/marker-operations.ts`

```typescript
export function generateOperationsFromMarkers(
  markers: ParsedMarker[],
  filePath: string,
  pairing: Pairing
): CreateOperation[]
```

**Logic:**
1. Filter out already-processed markers
2. Map marker metadata to HacknPlan fields
3. Resolve category/tag names to IDs using pairing
4. Generate description with source link
5. Return CreateOperation array

---

### Phase 3: Marker Injection (1h)

**File**: `src/lib/marker-injector.ts`

```typescript
export async function injectReviewMarker(
  filePath: string,
  marker: {
    type: 'needsreview' | 'outofsync' | 'completed';
    workItemId: number;
    date: Date;
    reason: string;
  }
): Promise<void>
```

**Injection Strategy:**
1. Read file content
2. Parse frontmatter
3. Find injection point (after frontmatter, before first heading)
4. Insert HTML comment marker
5. Write back atomically (temp file + rename)

---

### Phase 4: Tools (1h)

**Files**:
- `src/tools/marker-processing.ts`
- `src/tools/marker-review.ts`

Implement MCP tool handlers using parser, generator, and injector.

---

## Testing Strategy

### Unit Tests
```typescript
describe('MarkerParser', () => {
  it('parses #Todo marker with all parameters');
  it('parses #Bug marker with severity');
  it('ignores markers in code blocks');
  it('detects already-processed markers');
  it('handles malformed markers gracefully');
});

describe('OperationGenerator', () => {
  it('generates CreateOperation from #Todo');
  it('resolves category name to ID');
  it('resolves tag names to IDs');
  it('handles missing pairing mappings');
});

describe('MarkerInjector', () => {
  it('injects marker after frontmatter');
  it('preserves existing content');
  it('handles files without frontmatter');
  it('atomic write on failure');
});
```

### Integration Tests
```typescript
describe('End-to-End Marker Processing', () => {
  it('scans vault, finds markers, creates tickets');
  it('updates vault with ticket IDs');
  it('prevents duplicate processing');
  it('injects review markers on completion');
  it('clears markers after review');
});
```

---

## Configuration

### Pairing Extension

Add marker configuration to pairing:

```typescript
export interface Pairing {
  // ... existing fields
  markerConfig?: {
    enabled: boolean;
    autoProcess?: boolean;        // Auto-create tickets on vault scan
    markerTypes?: string[];       // Enabled marker types
    defaultCategory?: number;     // Fallback if not specified
    defaultPriority?: string;     // Fallback priority
    reviewMarkerLocation?: 'after-frontmatter' | 'before-first-heading' | 'end';
  };
}
```

**Example:**
```json
{
  "projectId": 230955,
  "vaultPath": "C:/vault",
  "markerConfig": {
    "enabled": true,
    "autoProcess": false,
    "markerTypes": ["todo", "bug", "feature"],
    "defaultCategory": 1,
    "defaultPriority": "normal",
    "reviewMarkerLocation": "after-frontmatter"
  }
}
```

---

## Usage Examples

### Example 1: Creating Tasks from Documentation

**Before:**
```markdown
# Phase 8 Implementation Plan

We need to:
1. Implement single-file sync
2. Add performance profiling
3. Optimize vault scanning
```

**After:**
```markdown
# Phase 8 Implementation Plan

We need to:
#Todo[programming|4h|optimization,phase-8]: Implement single-file sync for incremental updates
#Todo[programming|2h|profiling]: Add performance profiling to identify bottlenecks
#Todo[programming|3h|performance]: Optimize vault scanning with parallel processing
```

**Run:**
```bash
process_vault_markers --projectId 230955 --folder "05-Progress"
```

**Result:**
```
✅ Scanned 15 documents
✅ Found 3 markers
✅ Created 3 work items:
   - HP-34: Implement single-file sync for incremental updates
   - HP-35: Add performance profiling to identify bottlenecks
   - HP-36: Optimize vault scanning with parallel processing
```

**Updated Document:**
```markdown
#Todo[programming|4h|optimization,phase-8]: Implement single-file sync <!-- HP-34 -->
#Todo[programming|2h|profiling]: Add performance profiling <!-- HP-35 -->
#Todo[programming|3h|performance]: Optimize vault scanning <!-- HP-36 -->
```

---

### Example 2: Review Notifications

**Scenario**: Task HP-15 (File Watcher) completed

**Auto-Injected Marker:**
```markdown
---
title: File Watcher Architecture
hacknplan_id: 15
---

<!-- #NeedsReview[HP-15|2025-12-16]: File Watcher implementation complete - verify architecture docs -->

# File Watcher Architecture

The file watcher monitors Obsidian vault...
```

**Developer Workflow:**
```bash
# List all review markers
review_vault_markers --projectId 230955

# Output:
# 📋 Review Markers:
# 1. File Watcher Architecture - HP-15 (2025-12-16)
#    Reason: File Watcher implementation complete

# Update documentation...

# Clear marker after review
clear_vault_marker --projectId 230955 --filePath "05-Progress/mcp-development/glue-sync-engine.md" --workItemId 15
```

---

## Benefits

1. **Faster Workflow**: Create tickets without leaving documentation
2. **Context Preservation**: Tickets link back to design docs
3. **Review Automation**: Auto-notify when code changes require doc updates
4. **Searchability**: Markers are searchable in vault
5. **Traceability**: Full lifecycle tracking from idea → ticket → completion → review
6. **Single Source of Truth**: Documentation drives development

---

## Future Enhancements

### Phase 3 (Post-v1.0)
- [ ] Bidirectional updates: Update marker when HacknPlan ticket changes
- [ ] Marker templates: Define custom marker types per project
- [ ] AI-assisted marker generation: Suggest markers from commit messages
- [ ] Marker analytics: Dashboard showing marker→ticket conversion rates
- [ ] Integration with Git hooks: Process markers on commit
- [ ] Slack/Discord notifications: Alert team when #NeedsReview markers added

---

## References

**HacknPlan Tasks:**
- HP-32: Vault → HacknPlan marker parsing (4h)
- HP-33: HacknPlan → Vault review markers (3h)

**Related Files:**
- `src/lib/marker-parser.ts` - Parser implementation
- `src/lib/marker-operations.ts` - Operation generator
- `src/lib/marker-injector.ts` - Review marker injection
- `src/tools/marker-processing.ts` - MCP tools for processing
- `src/tools/marker-review.ts` - MCP tools for review

---

## Status: DOCUMENTED

Ready for implementation in Sprint 2 after Phase 9 completion.

**Priority Order:**
1. Complete Phase 8-9 (Integration Testing + Documentation)
2. Implement marker parsing (HP-32)
3. Implement review markers (HP-33)
4. Test end-to-end workflow
5. Document best practices
