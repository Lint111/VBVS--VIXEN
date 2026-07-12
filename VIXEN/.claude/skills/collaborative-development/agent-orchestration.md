# Agent Orchestration Rules

Detailed coordination patterns for multi-agent collaborative development.

## Agent Roles in Collaborative Workflow

### Architecture Critic
**Role:** Strategic oversight and consistency guardian

**Responsibilities:**
- Initial codebase exploration
- Pattern consistency checks
- Impact assessment
- Cross-cutting concern identification

**When to launch:**
- Phase 1: Discovery (always)
- Phase 5: Review (architectural changes only)
- Any time design questions arise

**Prompt template:**
```
Analyze the following feature request in context of the VIXEN codebase:
{feature_description}

Tasks:
1. Explore related files in libraries/ and identify affected components
2. Check Vixen-Docs/ for relevant architecture documentation
3. Identify patterns this feature should follow
4. Note potential impact on existing subsystems
5. Flag any architectural concerns or risks

Return:
- List of related files with file:line references
- Relevant doc links
- Recommended patterns to follow
- Risk assessment
```

---

### Coding Partner
**Role:** Implementation lead for complex logic

**Responsibilities:**
- Core algorithm implementation
- Complex refactoring
- Design pattern application
- Code quality ownership

**When to launch:**
- Phase 4: Implementation (complex subtasks)
- Phase 5: Review (code quality perspective)

**Prompt template:**
```
Implement the following subtask from the approved plan:
{subtask_description}

Context:
- Parent feature: {feature_name}
- Dependencies completed: {completed_subtasks}
- Related files: {file_list}

Requirements:
- Follow existing patterns in {pattern_reference}
- Maintain compatibility with {dependent_systems}
- Add appropriate logging using Logger system

Return when complete:
- Files modified with file:line references
- Any deviations from plan (with justification)
- Suggested tests to add
```

---

### Test Framework QA
**Role:** Quality assurance and test coverage

**Responsibilities:**
- Test plan creation
- Test implementation
- Coverage verification
- Regression detection

**When to launch:**
- Phase 3: Plan refinement (testability review)
- Phase 5: Review (coverage check)
- Phase 6: Testing (test execution)

**Prompt template for plan review:**
```
Review the following implementation plan for testability:
{plan_content}

Analyze:
1. Which components are testable as-is?
2. What mocking/stubbing is needed?
3. Integration test requirements?
4. Performance test needs?
5. Edge cases to cover?

Return:
- Test requirements list
- Suggested test structure
- Missing edge cases
- Recommended test utilities needed
```

**Prompt template for test creation:**
```
Create tests for the completed implementation:
{implementation_summary}

Files changed: {file_list}

Requirements:
1. Unit tests for new functions
2. Integration tests for component interactions
3. Update existing tests if interfaces changed
4. Follow GoogleTest patterns in existing tests

Return:
- Test files created/modified
- Coverage summary
- Any untestable areas (with reason)
```

---

### Intern Army (Refactor)
**Role:** Systematic repetitive changes

**Responsibilities:**
- Bulk renames
- Pattern application across files
- Boilerplate generation
- Consistency fixes

**When to launch:**
- Phase 4: Implementation (repetitive subtasks)
- Parallel execution of independent changes

**Prompt template:**
```
Apply the following systematic change across the codebase:
{change_description}

Scope: {file_pattern}

Rules:
1. Apply change consistently to ALL matching locations
2. Do not modify logic, only structure/names
3. Preserve existing formatting style
4. Report any locations that couldn't be changed automatically

Return:
- Files modified count
- Any exceptions or skipped locations
- Verification commands to run
```

---

### Project Maintainer
**Role:** Documentation hygiene and cleanup

**Responsibilities:**
- Remove temporary/scratch files
- Consolidate duplicate documentation
- Update cross-references
- Archive completed features
- Clean up memory-bank stale entries

**When to launch:**
- Phase 7: Completion (always)
- When documentation becomes bloated
- After major feature completion

**Prompt template:**
```
Clean up project documentation after feature completion:
Feature: {feature_name}
Feature doc: Vixen-Docs/05-Progress/features/{feature-name}.md

Tasks:
1. Archive completed feature doc to Vixen-Docs/05-Progress/completed/
2. Remove any temp files created during development
3. Consolidate scattered notes into canonical documentation
4. Update memory-bank/activeContext.md - remove completed items
5. Check for stale/outdated docs in 05-Progress/
6. Update cross-references in related docs

Return:
- Files archived/moved
- Files deleted (with justification)
- Docs updated
- Any cleanup deferred (with reason)
```

---

### Bug Hunter
**Role:** Deep debugging for complex issues

**Responsibilities:**
- Root cause analysis
- Systematic debugging
- Failure reproduction
- Fix verification

**When to launch:**
- When build fails unexpectedly
- When tests fail without obvious cause
- When runtime errors occur during implementation

**Prompt template:**
```
Debug the following failure:
{error_output}

Context:
- Feature being implemented: {feature_name}
- Recent changes: {recent_files}
- Last known good state: {last_success}

Tasks:
1. Reproduce the failure
2. Isolate root cause
3. Propose fix with minimal changes
4. Verify fix doesn't introduce new issues

Return:
- Root cause analysis
- Fix applied (file:line)
- Verification results
```

---

## Orchestration Patterns

### Sequential Dependency Chain
```
Task A (blocking) → Task B (depends on A) → Task C (depends on B)
```
Launch one at a time, wait for completion.

### Parallel Independent Tasks
```
Task A ─┬─→ Task D (waits for all)
Task B ─┤
Task C ─┘
```
Launch A, B, C simultaneously with multiple Task tool calls.

### Review Cycle
```
Implementation → Review → [Fix if needed] → Re-review → Approve
```
Loop until all reviewers approve.

---

## Concurrency Limits

| Scenario | Max Parallel Agents |
|----------|---------------------|
| Implementation phase | 3 |
| Review phase | 2 (architecture + qa) |
| Testing phase | 1 |
| Bug hunting | 1 (focused debugging) |

---

## Agent Communication Pattern

Agents cannot communicate directly. Main Claude orchestrates:

1. **Launch Agent A** → Receive results
2. **Synthesize findings** into context for Agent B
3. **Launch Agent B** with context → Receive results
4. **Combine outputs** for user or next phase

### Context Passing Template
```
Previous phase findings:
- Architecture review: {arch_findings}
- Code quality review: {code_findings}
- QA review: {qa_findings}

Your task builds on this context:
{new_task}
```

---

## Error Escalation

### Agent Timeout
- Log the timeout
- Report partial results if available
- Offer to retry or proceed manually

### Agent Conflict
When reviewers disagree:
1. Present both perspectives to user
2. User decides resolution
3. Document decision rationale

### Agent Failure
- Capture error state
- Switch to manual implementation
- Document what was attempted
