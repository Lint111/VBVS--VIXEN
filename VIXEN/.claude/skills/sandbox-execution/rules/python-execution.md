# Python Code Execution via code-executor MCP

Rules for using `mcp__code-executor__executePython` to call other MCP tools.

## Correct Usage Pattern

```python
# ✅ CORRECT: Use await directly (already in async context)
result = await call_mcp_tool('mcp__hacknplan__create_work_items', {
    'projectId': 230809,
    'items': [{
        'title': 'Task title',
        'categoryId': 1
    }]
})
print("Result:", result)
```

## Common Mistakes (BAD Examples)

### Mistake 1: Not awaiting the coroutine

```python
# ❌ BAD: Returns coroutine object, not actual result
result = call_mcp_tool('mcp__hacknplan__create_work_items', {...})
print("Result:", result)  # Prints: <coroutine object call_mcp_tool at 0x...>
```

**Why it fails:** `call_mcp_tool` is async and returns a coroutine. Without `await`, you get the coroutine object instead of the result.

### Mistake 2: Using asyncio.run() inside async context

```python
# ❌ BAD: Raises RuntimeError
import asyncio

async def create_task():
    result = await call_mcp_tool(...)
    return result

result = asyncio.run(create_task())  # RuntimeError: cannot be called from running event loop
```

**Why it fails:** The code-executor already runs in an async event loop. `asyncio.run()` tries to create a new one, which fails.

### Mistake 3: Using singular tool names (slim mode legacy)

```python
# ❌ BAD: Tool not found error
result = await call_mcp_tool('mcp__hacknplan__create_work_item', {...})
# Exception: Tool not found: mcp__hacknplan__create_work_item
```

**Why it fails:** HacknPlan MCP uses batch operations. Tool names are plural: `create_work_items`, `update_work_items`, not `create_work_item`.

### Mistake 4: Wrong parameter structure for batch operations

```python
# ❌ BAD: Passing fields directly instead of in items array
result = await call_mcp_tool('mcp__hacknplan__create_work_items', {
    'projectId': 230809,
    'title': 'Task title',  # Wrong! Should be in items array
    'categoryId': 1
})
```

**Correct structure:**
```python
# ✅ CORRECT: Batch operations use items array
result = await call_mcp_tool('mcp__hacknplan__create_work_items', {
    'projectId': 230809,
    'items': [{
        'title': 'Task title',
        'categoryId': 1
    }]
})
```

## MCP Tool Discovery

When unsure of available tools, check the error message for the full list:

```python
# Trigger intentional error to see available tools
result = await call_mcp_tool('mcp__hacknplan__nonexistent', {})
# Error shows: Available tools: mcp__hacknplan__list_projects, mcp__hacknplan__create_work_items, ...
```

## Required Parameters

Always include `allowedTools` parameter in the execution call:

```python
mcp__code-executor__executePython({
    'code': '...',
    'allowedTools': ['mcp__hacknplan__create_work_items'],  # REQUIRED
    'timeoutMs': 30000
})
```

## Template for HacknPlan Operations

```python
# Create task
result = await call_mcp_tool('mcp__hacknplan__create_work_items', {
    'projectId': 230809,
    'items': [{
        'title': '[Component] Task description',
        'categoryId': 1,  # Programming
        'boardId': 649722,  # Current sprint
        'stageId': 1,  # Planned
        'importanceLevelId': 3,  # Normal
        'estimatedCost': 4,
        'description': 'Full description here',
        'tagIds': [1, 2],
        'assignedUserIds': [230909]
    }]
})
print(f"Created: #{result['items'][0]['workItemId']}")

# Update task
result = await call_mcp_tool('mcp__hacknplan__update_work_items', {
    'projectId': 230809,
    'items': [{
        'workItemId': 172,
        'stageId': 4,  # Completed
        'isCompleted': True
    }]
})

# Complete task with comment
result = await call_mcp_tool('mcp__hacknplan__complete_task', {
    'projectId': 230809,
    'workItemId': 172,
    'comment': 'Work completed successfully'
})
```

## Parallel Execution Pattern

For multiple independent MCP calls, use `asyncio.gather()` for parallel execution:

```python
import asyncio
import json

async def parallel_queries():
    # Launch multiple calls simultaneously
    tasks = [
        call_mcp_tool('mcp__hacknplan__list_projects', {}),
        call_mcp_tool('mcp__hacknplan__list_boards', {'projectId': 230809}),
        call_mcp_tool('mcp__hacknplan__list_work_items', {'projectId': 230809, 'boardId': 649722})
    ]

    # Await all at once - runs in parallel
    results = await asyncio.gather(*tasks)

    # Parse results (returned as JSON strings)
    # Note: Response may be dict with 'items' key OR a direct list
    def parse_result(r):
        data = json.loads(r) if isinstance(r, str) else r
        if isinstance(data, dict):
            return data.get('items', [])
        elif isinstance(data, list):
            return data
        return []

    projects = parse_result(results[0])
    boards = parse_result(results[1])
    tasks = parse_result(results[2])

    return projects, boards, tasks

projects, boards, tasks = await parallel_queries()
```

**Performance:** 3 parallel calls complete in ~100ms vs ~300ms sequential.

## MCP Connection Handling

### Connection Check Before Operations

Always check connectivity before critical operations:

```python
import json

class MCPConnectionError(Exception):
    """Raised when MCP is not connected"""
    pass

async def check_mcp_connection():
    """Test MCP connectivity. Raises MCPConnectionError if disconnected."""
    try:
        result = await call_mcp_tool('mcp__hacknplan__list_projects', {})
        data = json.loads(result) if isinstance(result, str) else result
        if isinstance(data, (list, dict)):
            return True  # Connected
    except Exception as e:
        error_msg = str(e)
        if any(x in error_msg for x in ['Not connected', 'Tool not found', 'MCP error']):
            raise MCPConnectionError(
                "MCP DISCONNECTED: HacknPlan MCP is not available.\n"
                "ACTION REQUIRED: Please run `/mcp` to check server status and reconnect.\n"
                "After reconnecting, retry your request."
            )
        raise  # Re-raise other errors

# Usage pattern
try:
    await check_mcp_connection()
    # Proceed with actual work...
    result = await call_mcp_tool('mcp__hacknplan__create_work_items', {...})
except MCPConnectionError as e:
    print(f"⚠️ {e}")
    print("\n🛑 PAUSING WORK - Manual intervention required")
    raise  # Stop execution
```

### Retry Pattern (Optional)

For transient failures, retry up to 2 times:

```python
async def mcp_call_with_retry(tool_name, params, max_retries=2):
    """Call MCP tool with retry logic for transient failures."""
    last_error = None
    for attempt in range(max_retries + 1):
        try:
            result = await call_mcp_tool(tool_name, params)
            return json.loads(result) if isinstance(result, str) else result
        except Exception as e:
            last_error = e
            error_msg = str(e)
            # Don't retry connection errors - need user intervention
            if any(x in error_msg for x in ['Not connected', 'Tool not found']):
                raise MCPConnectionError(f"MCP disconnected: {error_msg}")
            if attempt < max_retries:
                print(f"Retry {attempt + 1}/{max_retries} after error: {error_msg}")
                continue
    raise last_error

# Usage
result = await mcp_call_with_retry('mcp__hacknplan__list_work_items', {
    'projectId': 230809
})
```

### Error Categories

| Error Type | Action | Retry? |
|------------|--------|--------|
| `Not connected` | Ask user to run `/mcp` | No |
| `Tool not found` | Check tool name spelling | No |
| `MCP error -32000` | Server crashed, restart needed | No |
| `Timeout` | Transient, retry | Yes (2x) |
| `Rate limit` | Wait and retry | Yes (with backoff) |
