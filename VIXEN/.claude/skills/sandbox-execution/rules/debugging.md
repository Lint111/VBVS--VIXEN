# Sandbox Debugging Standards

## Logging Requirements

All sandbox scripts must use structured logging:

```typescript
import { log } from 'sandbox/logging';

// Levels: debug, info, warn, error
log.info('Fetching tasks', { projectId, sprintId });
log.debug('API response', { taskCount: tasks.length });
log.error('Failed to fetch', { error: e.message, stack: e.stack });
```

## Log Output Location

Logs written to `result.json` under `logs` array:

```json
{
  "logs": [
    { "level": "info", "msg": "Fetching tasks", "data": {...}, "ts": "..." },
    { "level": "debug", "msg": "API response", "data": {...}, "ts": "..." }
  ]
}
```

## Error Handling

**No silent failures.** Every error must:

1. Be logged with context
2. Be included in `result.errors[]`
3. Set appropriate `result.status`

```typescript
try {
  const tasks = await hacknplan.getTasks(sprintId);
} catch (e) {
  log.error('Task fetch failed', {
    sprintId,
    error: e.message,
    recoverable: false
  });

  return {
    status: 'failure',
    errors: [{
      operation: 'getTasks',
      message: e.message,
      fatal: true
    }]
  };
}
```

## Partial Success Handling

When some operations succeed and others fail:

```typescript
const results = await Promise.allSettled(operations);

const succeeded = results.filter(r => r.status === 'fulfilled');
const failed = results.filter(r => r.status === 'rejected');

return {
  status: failed.length === 0 ? 'success' : 'partial',
  output: {
    summary: `${succeeded.length}/${results.length} operations completed`,
    successfulOps: succeeded.map(r => r.value),
  },
  errors: failed.map(r => ({
    operation: r.reason.operation,
    message: r.reason.message
  }))
};
```

## Debug Mode

Scripts can enable verbose mode for troubleshooting:

```typescript
const DEBUG = context.params.debug ?? false;

if (DEBUG) {
  log.debug('Raw API response', { body: response });
  log.debug('Parsed data', { parsed });
}
```

Enable via context: `{ "params": { "debug": true } }`

## Required Return Shape

All scripts must return typed results:

```typescript
interface SandboxResult<T> {
  status: 'success' | 'failure' | 'partial';
  output: {
    summary: string;      // Human-readable one-liner
    data: T;              // Typed payload
  };
  errors: Array<{
    operation: string;
    message: string;
    fatal?: boolean;
  }>;
  metrics?: {
    operationsPerformed: number;
    apiCallsMade: number;
    durationMs: number;
  };
}
```

## Traceability

Include correlation IDs when available:

```typescript
log.info('Processing request', {
  correlationId: context.correlationId,
  taskId: context.relatedTask
});
```

This enables tracing sandbox execution back to originating user request.
