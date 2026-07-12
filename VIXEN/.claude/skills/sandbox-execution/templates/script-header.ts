/**
 * Sandbox Script Template
 *
 * Copy this header for all sandbox scripts. Required fields ensure
 * proper archival and traceability.
 */

import { log, SandboxResult, SandboxContext } from 'sandbox/core';

// ============================================================================
// SCRIPT METADATA (Required)
// ============================================================================

const SCRIPT_META = {
  purpose: '',           // Human-readable: "Fetch sprint tasks and format summary"
  category: '',          // hacknplan | obsidian | filesystem | git | mixed
  destructive: false,    // true if modifies external state
  mcpServers: [] as string[],  // ['hacknplan', 'obsidian-vault']
};

// ============================================================================
// TYPE DEFINITIONS
// ============================================================================

interface ScriptParams {
  // Define expected input parameters
  // Example: projectId: string;
}

interface ScriptOutput {
  // Define output data shape
  // Example: tasks: Task[];
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================

export async function execute(
  context: SandboxContext,
  params: ScriptParams
): Promise<SandboxResult<ScriptOutput>> {

  log.info('Script started', { purpose: SCRIPT_META.purpose, params });

  try {
    // ========================================================================
    // IMPLEMENTATION
    // ========================================================================

    // Your code here...

    // ========================================================================
    // SUCCESS RETURN
    // ========================================================================

    return {
      status: 'success',
      output: {
        summary: 'Describe what was accomplished',
        data: {
          // Your typed output
        } as ScriptOutput,
      },
      errors: [],
      metrics: {
        operationsPerformed: 0,
        apiCallsMade: 0,
        durationMs: Date.now() - context.startTime,
      },
    };

  } catch (error) {
    // ========================================================================
    // ERROR RETURN
    // ========================================================================

    log.error('Script failed', {
      error: error.message,
      stack: error.stack
    });

    return {
      status: 'failure',
      output: {
        summary: `Failed: ${error.message}`,
        data: {} as ScriptOutput,
      },
      errors: [{
        operation: 'execute',
        message: error.message,
        fatal: true,
      }],
    };
  }
}

// ============================================================================
// DESTRUCTIVE OPERATION EXAMPLE (if needed)
// ============================================================================

/*
import { requestDestructive } from 'sandbox/permissions';

// When you need to perform destructive operations:
const confirmed = await requestDestructive({
  operation: 'delete',
  targets: ['/path/to/file'],
  reason: 'Cleaning up temporary artifacts',
});

if (!confirmed) {
  return {
    status: 'failure',
    output: { summary: 'User denied destructive operation', data: {} },
    errors: [{ operation: 'delete', message: 'Denied by user', fatal: false }],
  };
}

// Proceed with operation...
*/
