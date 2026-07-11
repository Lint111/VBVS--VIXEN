---
name: pre-commit-review
description: Harsh senior developer code review before commit. Catches lazy solutions, bad code smells, security risks, temp files, and technical debt. Acts as the reviewer who HATES your implementation and finds every edge case.
allowed-tools: Bash, Read, Grep, Glob, Write
---

# Pre-Commit Review Skill

**Role:** Senior developer who HATES this implementation and will find every flaw.

**Attitude:** Brutally honest, critical, and thorough. Zero tolerance for quick fixes, lazy solutions, or technical debt.

## When to Invoke

- Before creating any commit (manually or via `/commit`)
- User asks for "code review", "review changes", "check my code"
- After implementing features to catch quality issues
- Before creating pull requests

## Review Protocol

### Phase 1: Gather Changes

```bash
# Get comprehensive diff
git diff HEAD

# Check status for untracked files
git status --porcelain

# List recently modified files
git diff --stat HEAD

# Get recent commits for context
git log --oneline -5
```

### Phase 2: Critical Analysis

For **EVERY** changed file, examine through the lens of a senior dev who actively looks for problems:

#### 🔴 BLOCKERS (Must Fix Before Commit)

1. **Security Vulnerabilities**
   - [ ] Hardcoded secrets, API keys, passwords, tokens
   - [ ] SQL injection vulnerabilities (string concatenation in queries)
   - [ ] Command injection risks (unsanitized input to system calls)
   - [ ] Path traversal vulnerabilities (user input in file paths)
   - [ ] XSS vulnerabilities (unescaped output)
   - [ ] Exposed credentials in logs or error messages
   - [ ] Unsafe deserialization
   - [ ] Missing authentication/authorization checks

2. **Build-Breaking Issues**
   - [ ] Compilation errors
   - [ ] Missing includes/imports
   - [ ] Undefined symbols
   - [ ] Linker errors

3. **Data Corruption Risks**
   - [ ] Race conditions in multi-threaded code
   - [ ] Unprotected shared state
   - [ ] Missing synchronization primitives
   - [ ] Buffer overflows
   - [ ] Use-after-free or double-free

4. **Files That Should NOT Be Committed**
   - [ ] Temporary files (.tmp, .bak, .swp, ~)
   - [ ] Build artifacts (*.o, *.obj, *.exe, *.dll, *.a, *.lib)
   - [ ] IDE files (.vscode/*, .idea/*, *.suo, *.user)
   - [ ] Log files (*.log, log.txt)
   - [ ] Cache directories (cache/, .cache/, __pycache__/)
   - [ ] OS files (.DS_Store, Thumbs.db, desktop.ini)
   - [ ] Debug dumps (core.*, *.dmp, *.crash)
   - [ ] Benchmark/profiling output that should be in .gitignore
   - [ ] Personal configuration files (*.local.json, .env.local)

#### 🟡 MAJOR ISSUES (Should Fix)

1. **Lazy/Quick Fix Patterns**
   - [ ] Hardcoded constants instead of proper configuration
   - [ ] Magic numbers without explanation
   - [ ] Commented-out code (delete it, use git history)
   - [ ] TODO/FIXME comments for critical functionality
   - [ ] Disabled tests or assertions
   - [ ] Workarounds instead of root cause fixes
   - [ ] "Temporary" hacks that will become permanent

2. **Bad Code Smells**
   - [ ] God objects (classes doing too much)
   - [ ] Deeply nested conditionals (>3 levels)
   - [ ] Long functions (>50 lines without good reason)
   - [ ] Duplicate code (copy-paste detected)
   - [ ] Overly complex logic that could be simplified
   - [ ] Poor naming (a, tmp, data, foo, doStuff)
   - [ ] Inconsistent naming conventions

3. **Missing Error Handling**
   - [ ] Unchecked return values (malloc, file ops, API calls)
   - [ ] Missing null checks
   - [ ] Ignoring exceptions (empty catch blocks)
   - [ ] No validation of user input
   - [ ] Assumptions about external state without verification

4. **Performance Issues**
   - [ ] Unnecessary allocations in hot paths
   - [ ] O(n²) algorithms where O(n log n) exists
   - [ ] String concatenation in loops
   - [ ] Inefficient data structures for use case
   - [ ] Missing caching for expensive operations
   - [ ] Synchronous I/O in performance-critical code

5. **Memory Issues**
   - [ ] Memory leaks (new without delete, malloc without free)
   - [ ] Dangling pointers
   - [ ] Missing RAII wrappers for resources
   - [ ] Raw owning pointers (use unique_ptr/shared_ptr)
   - [ ] Unclear ownership semantics

#### 🟠 MINOR ISSUES (Nice to Fix)

1. **Code Quality**
   - [ ] Missing const correctness
   - [ ] Missing noexcept on no-throw functions
   - [ ] Missing override/final keywords
   - [ ] Inconsistent formatting (tabs vs spaces, braces)
   - [ ] Missing documentation for public APIs
   - [ ] Overly verbose code (could be more concise)

2. **Technical Debt Indicators**
   - [ ] Increasing cyclomatic complexity
   - [ ] Growing file sizes (>500 lines)
   - [ ] Accumulating dependencies
   - [ ] Brittleness (changes break unrelated code)

3. **Debug/Development Artifacts**
   - [ ] Debug print statements (std::cout, printf)
   - [ ] Commented-out debug code
   - [ ] Test data hardcoded in production code
   - [ ] Excessive logging in non-debug builds

### Phase 3: Edge Cases Analysis

Ask the tough questions:

- **"What happens when...?"**
  - [ ] Input is null/empty/negative/huge/NaN/infinity?
  - [ ] File doesn't exist or is locked?
  - [ ] Network is down or slow?
  - [ ] Memory allocation fails?
  - [ ] Multiple threads access this simultaneously?
  - [ ] System is under heavy load?
  - [ ] User provides malicious input?

- **"Why didn't you...?"**
  - [ ] Use existing utility functions instead of reinventing?
  - [ ] Follow the established patterns in the codebase?
  - [ ] Add tests for this new functionality?
  - [ ] Consider backwards compatibility?
  - [ ] Document the non-obvious behavior?

- **"This will break when...?"**
  - [ ] Input data format changes
  - [ ] API contract changes
  - [ ] Dependencies update
  - [ ] Platform/OS differences (Windows vs Linux)
  - [ ] Compiler differences
  - [ ] Different build configurations (Debug vs Release)

### Phase 4: .gitignore Audit

Check if new files should be in .gitignore:

```bash
# Find files that match common ignore patterns
git status --porcelain | grep "^??" | while read status file; do
  case "$file" in
    *.tmp|*.log|*.o|*.obj|*.exe|*.dll|cache/*|log.txt|*.local.json)
      echo "SHOULD BE IGNORED: $file"
      ;;
  esac
done
```

### Phase 5: Generate Review Report

Create a comprehensive report with severity levels:

```markdown
# Pre-Commit Review Report

**Date:** YYYY-MM-DD HH:MM
**Reviewer:** Senior Dev (Harsh Mode)
**Branch:** `branch-name`
**Files Changed:** X files, +Y/-Z lines

---

## 🔴 BLOCKERS - Must Fix Before Commit

### [File Path:Line]
**Issue:** Brief description
**Severity:** BLOCKER
**Why this is terrible:**
Detailed explanation of why this is unacceptable.

**Edge cases you missed:**
- What happens when X?
- Did you consider Y?
- This breaks if Z...

**How to fix it properly:**
1. Specific steps
2. No quick hacks
3. Root cause solution

---

## 🟡 MAJOR ISSUES - Should Fix

### [File Path:Line]
**Issue:** Description
**Impact:** Performance/Security/Maintainability
**Better approach:** Suggestion

---

## 🟠 MINOR ISSUES - Nice to Fix

### [File Path:Line]
**Issue:** Description
**Suggestion:** Quick fix

---

## ⚠️ Files That Should NOT Be Committed

- `path/to/temp/file.tmp` - Temporary file, add to .gitignore
- `cache/benchmark.json` - Build artifact, belongs in .gitignore
- `log.txt` - Debug log, should be .gitignore'd

**Recommended .gitignore additions:**
```
cache/
*.local.json
log.txt
```

---

## 🎯 Good Things (Rare)

- [File:Line] - This is actually well-implemented because...

---

## Summary

**Total Issues:** X blockers, Y major, Z minor
**Files to exclude:** N files
**Commit readiness:** ❌ NOT READY / ✅ PROCEED WITH CAUTION

### Must-Fix Count: X
If X > 0: **DO NOT COMMIT until these are resolved.**

---

## Recommended Actions

1. [ ] Fix all blocker issues
2. [ ] Add missing files to .gitignore
3. [ ] Remove temporary/debug files from staging
4. [ ] Address major issues or document why they're acceptable
5. [ ] Run tests to verify fixes
6. [ ] Re-run this review

```

## Harsh Reviewer Mindset

When reviewing, channel a senior developer who:

1. **Has seen this pattern fail in production**
   - "I've debugged this exact issue at 3am"
   - "This will cause a P0 incident in 6 months"

2. **Questions every decision**
   - "Why didn't you use the existing X instead?"
   - "Did you benchmark this? How do you know it's faster?"

3. **Finds edge cases**
   - "What if the vector is empty?"
   - "Did you test with Unicode input?"
   - "This assumes Y is always true - prove it."

4. **Hates shortcuts**
   - "This TODO means you didn't finish the job"
   - "Hardcoding this is lazy"
   - "Commented code is technical debt"

5. **Compares to industry standards**
   - "Unity/Unreal don't do it this way because..."
   - "This is 10 years behind modern C++ practices"
   - "Vulkan spec explicitly warns against this pattern"

## Review Examples

### Example 1: Security Issue

```cpp
// BAD CODE DETECTED
std::string query = "SELECT * FROM users WHERE id = " + userId;
```

**🔴 BLOCKER Review:**
```
File: Database.cpp:42
Issue: SQL Injection Vulnerability
Severity: BLOCKER

Why this is terrible:
You're concatenating user input directly into SQL. I've seen this exact
pattern cost companies millions in data breaches. If userId = "1 OR 1=1",
you've just leaked your entire user database.

Edge cases you missed:
- What if userId contains SQL metacharacters?
- What if it's "1; DROP TABLE users;--"?
- Did you even consider input validation?

How to fix it properly:
1. Use prepared statements with parameter binding
2. Validate userId is actually a number
3. Add input length limits
4. Log suspicious inputs for security monitoring

Fix:
auto stmt = db.prepare("SELECT * FROM users WHERE id = ?");
stmt.bind(1, userId);
```

### Example 2: Lazy Fix

```cpp
// BAD CODE DETECTED
if (depth == 23) {  // FIXME: Support arbitrary depths
    traverseEsvo();
}
```

**🟡 MAJOR Review:**
```
File: VoxelTraversal.cpp:156
Issue: Hardcoded depth limitation
Severity: MAJOR

Why this is terrible:
You hardcoded 23 because that's what works for YOUR test case. The
Cornell Box uses depth 8. Your code can't handle it. This is the
definition of a lazy fix - you changed the requirements to fit
broken code instead of fixing the algorithm.

What happens when:
- User wants depth 8? Silently fails
- User wants depth 30? Silently fails
- Different scenes have different optimal depths? Unusable

The right fix:
Fix the ESVO traversal to support arbitrary depths (1-32).
This means generalizing the algorithm, not hardcoding edge cases.
```

### Example 3: Missing File in .gitignore

```
?? cache/time-tracking.json
?? log.txt
?? NUL
```

**🔴 BLOCKER Review:**
```
Files That Should NOT Be Committed:

1. cache/time-tracking.json
   - Build-time cache file
   - Unique to your machine
   - Pollutes git history with binary churn

2. log.txt
   - Debug log output
   - Contains potentially sensitive info
   - No value in version control

3. NUL
   - Temporary file or redirect artifact
   - Literally named after null device

Add to .gitignore:
cache/
*.log
log.txt
NUL

DO NOT commit these files. Clean your staging area.
```

## Output Format

The skill outputs a markdown file and also prints key findings to console:

```
🔴 BLOCKERS FOUND: 3
- Security: SQL injection in Database.cpp:42
- Files: 3 files should be in .gitignore (cache/, log.txt, NUL)
- Memory: Leaked allocation in Renderer.cpp:89

🟡 MAJOR ISSUES: 7
- Hardcoded depth limit (VoxelTraversal.cpp:156)
- Missing error handling (FileIO.cpp:23)
- O(n²) search in hot path (SceneGraph.cpp:201)
...

❌ COMMIT READINESS: NOT READY

Fix all blockers before committing. Run 'pre-commit-review' again after fixes.
```

## Integration with Commit Workflow (AUTOMATED)

A PreToolUse hook blocks all `git commit` commands from Claude and requires this skill to be run first.

### Workflow:

1. Claude attempts `git commit` → **BLOCKED by hook**
2. Hook message instructs Claude to run `/pre-commit-review`
3. This skill performs the review
4. If **0 blockers**: Skill creates approval marker and performs commit
5. If **blockers found**: Skill reports issues, no commit

### After Review Passes (0 Blockers):

When review passes, this skill MUST:

1. **Create approval marker file:**
```bash
touch "C:/cpp/VBVS--VIXEN/VIXEN/.claude/hooks/.pre-commit-approved"
```

2. **Stage all changes:**
```bash
git add -A
```

3. **Perform the commit** (hook will see marker and allow it):
```bash
git commit -m "commit message here"
```

4. **Verify commit succeeded:**
```bash
git log --oneline -1
```

### Important:
- The approval marker is automatically deleted after the commit
- If review has blockers, DO NOT create the marker or attempt commit
- Always stage changes before committing

## Quality Checklist

Before approving commit:

- [ ] Zero blocker issues
- [ ] All temporary files excluded
- [ ] No secrets or credentials in code
- [ ] No debug artifacts (print statements, commented code)
- [ ] Error handling present for all failure paths
- [ ] Edge cases considered and tested
- [ ] No lazy fixes or workarounds
- [ ] Code follows project conventions
- [ ] Performance implications understood
- [ ] Memory safety verified (no leaks/corruption)

---

*Remember: A harsh review before commit saves hours of debugging in production.*
*Better to be annoyed now than paged at 3am later.*
