# Gemini CLI Large Context Analysis

> **Skill:** `gemini-codebase-analysis`
> **Purpose:** Analyze entire codebases using Gemini's 2M+ token context window
> **Location:** `.claude/skills/gemini-codebase-analysis/`

## Overview

The Gemini CLI analysis skill enables Claude to offload large-scale codebase analysis to Google Gemini when the scope exceeds Claude's context window. This creates a powerful complementary workflow:

- **Gemini:** Wide context analysis (entire codebase, 50+ files, pattern detection)
- **Claude:** Deep analysis (code writing, debugging, optimization)

## When to Use

### ✅ Use Gemini CLI when:
- Analyzing 50+ files simultaneously
- Verifying feature implementation across entire codebase
- Security auditing project-wide patterns
- Architecture understanding requires broad context
- Pattern detection across multiple directories
- Data exceeds 100KB or Claude's context limit

### ❌ Don't use when:
- Analyzing 1-5 specific files (use Claude's Read tool)
- Writing or modifying code (Claude handles this)
- Debugging specific logic errors (Claude excels here)
- Deep algorithmic analysis (Claude is better)

## Installation

### 1. Install Gemini CLI

```bash
npm install -g @google/generative-ai-cli
```

### 2. Configure API Key

Get your API key from https://ai.google.dev

```powershell
# Windows
setx GOOGLE_API_KEY "your-api-key-here"
```

### 3. Verify Installation

```bash
gemini --version
```

## Usage in Claude Code

### Invoke via Skill

```
User: "Use gemini-codebase-analysis to check if authentication is fully implemented"
```

Claude will:
1. Construct appropriate Gemini CLI query
2. Execute analysis with `@` syntax for file inclusion
3. Parse and validate results
4. Provide actionable recommendations
5. Document findings in Obsidian

### Manual Gemini Query

For testing or direct use:

```bash
cd C:\cpp\VBVS--VIXEN
gemini -p "@VIXEN/libraries/ List all Vulkan synchronization primitives and their usage"
```

## Common Use Cases

### 1. Security Audit

```
User: "Run a security audit on the entire VIXEN codebase"
```

Gemini will scan for:
- Hardcoded secrets
- SQL injection risks
- Missing validation
- Unsafe Vulkan API usage

### 2. Feature Verification

```
User: "Check if descriptor set caching is implemented everywhere"
```

Gemini will:
- Find all descriptor set allocations
- Verify caching strategy consistency
- Identify missing cache implementations

### 3. Test Coverage Analysis

```
User: "Which libraries lack unit tests?"
```

Gemini will:
- Scan all library modules
- Check for corresponding test files
- Create coverage matrix

### 4. Pattern Detection

```
User: "Find all places where we're not using VK_CHECK for Vulkan calls"
```

Gemini will:
- Scan all Vulkan API calls
- Identify missing error checks
- List files and line numbers

## VIXEN-Specific Examples

### Vulkan Synchronization

```bash
gemini -p "@VIXEN/libraries/ @VIXEN/VixenBenchmark/ Are all vkQueueSubmit calls properly synchronized? List all fence and semaphore usage"
```

### Shader Management

```bash
gemini -p "@VIXEN/shaders/ @VIXEN/libraries/RenderGraph/ Are all GLSL shaders compiled and loaded? List any orphaned shaders"
```

### Memory Management

```bash
gemini -p "@VIXEN/libraries/ Identify all VkBuffer and VkImage allocations. Check for corresponding deallocations"
```

### Resource Lifetime

```bash
gemini -p "@VIXEN/libraries/ Track VkDevice resource lifetimes. Flag resources created but not destroyed"
```

## Query Patterns

See `.claude/skills/gemini-codebase-analysis/example-queries.md` for comprehensive templates.

### Basic Pattern

```bash
gemini -p "@<directory>/ <specific question>"
```

### Multiple Directories

```bash
gemini -p "@src/ @lib/ @tests/ <question>"
```

### Requesting Structured Output

```bash
gemini -p "@src/api/ List all API endpoints as markdown table: Route | Method | Auth | Description"
```

## Rate Limits

**Gemini Free Tier:**
- 60 requests per minute
- 1,000 requests per day

**Strategy:** Construct comprehensive queries to minimize request count.

## Output Format

Skill generates structured markdown reports:

```markdown
# Gemini Analysis: {{TOPIC}}

**Date:** {{DATE}}
**Scope:** {{DIRECTORIES}}

## Summary
[One paragraph overview]

## Findings

### ✓ Implemented Features
- Feature 1: file.cpp:42 - Description

### ⚠ Gaps Identified
- Missing validation in file.cpp:123

### ✗ Security Concerns
- Hardcoded secret in config.h:8

## Recommendations
1. [Actionable item 1] (Priority: High)
2. [Actionable item 2] (Priority: Medium)

## Next Steps
- [ ] Create HacknPlan tasks
- [ ] Update architecture docs
```

## Integration with Other Skills

### [[Session-Workflow-Integration|Session Workflow]]
- Phase 1: Use Gemini for codebase understanding
- Phase 2-3: Feed findings into planning

### [[HacknPlan-Integration|HacknPlan]]
- Convert critical findings to work items
- Tag with `security`, `technical-debt`, `bug`

### [[Testing|Testing Strategy]]
- Use Gemini to identify test coverage gaps
- Create work items for missing tests

## Best Practices

### 1. Be Specific
❌ `gemini -p "@src/ Tell me about the code"`
✅ `gemini -p "@src/ List all React components that fetch API data"`

### 2. Request Structure
❌ `gemini -p "@src/ Find problems"`
✅ `gemini -p "@src/ Create markdown table of functions lacking error handling"`

### 3. Scope Appropriately
❌ `gemini -p "@./ @node_modules/ Analyze everything"` (wastes tokens)
✅ `gemini -p "@src/ @lib/ Analyze application code"` (focused)

### 4. Validate Results
- Spot-check Gemini findings with Claude's Read tool
- Verify critical security issues manually

### 5. Document Findings
- Store analysis in `Vixen-Docs/Analysis/Gemini/`
- Cross-reference with HacknPlan tasks

## Troubleshooting

### "gemini: command not found"
```bash
npm install -g @google/generative-ai-cli
# Restart terminal
```

### Rate limit exceeded
- Wait before retrying (60/min limit)
- Or upgrade Gemini plan at https://ai.google.dev/pricing

### Generic responses
- Make query more specific
- Include project context: "This is a Vulkan rendering engine..."
- Request structured output format

### Context too large
- Break into multiple focused queries
- Analyze subsystems separately
- Exclude build artifacts and dependencies

## Resources

- **Skill Definition:** `.claude/skills/gemini-codebase-analysis/skill.md`
- **Example Queries:** `.claude/skills/gemini-codebase-analysis/example-queries.md`
- **Gemini CLI Docs:** https://github.com/google/generative-ai-docs
- **Gemini API Docs:** https://ai.google.dev/docs

## Related Documentation

- [[Data-Visualization-Pipeline|Data Visualization Pipeline]] - Complementary data analysis
- [[Testing|Testing Strategy]] - Test coverage verification
- [[Build-System|Build System]] - Integration with build workflows

---

**Tags:** #gemini #analysis #codebase #skill #claude-code
**Created:** 2025-12-23
**Last Updated:** 2025-12-23
# Resources
## Test Results

### Successful Test: VIXEN Architecture Analysis (2025-12-23)

**Command:**
```bash
gemini -p "@C:\cpp\VBVS--VIXEN\VIXEN/ Summarize the architecture of this codebase"
```

**Execution Time:** ~60-90 seconds

**Results:** ✅ Success
- Complete architectural overview
- Identified modular library structure
- Described RenderGraph pattern
- Noted Shader-Defined Interface (SDI) system
- Analyzed build system architecture

**Key Insights from Analysis:**
- Modular design with 15+ independent libraries
- Graph-based rendering via RenderGraph
- Advanced voxel engine (GaiaVoxelWorld, SVO)
- Type-safe CPU-GPU interface via SDI
- Mature CMake build system with packaging

**Output Quality:** High - comprehensive and accurate architectural summary

**Working Syntax:**
- Use `@` with absolute paths
- Include trailing `/` for directories
- Long timeout required (60-120 seconds)
- Works with large codebases (VIXEN: 15+ libraries, 100+ files)

# Execution Time
**Execution Time:** ~90-120 seconds actual, **recommend 180000-300000ms (3-5 min) timeout**

**Rationale:** 
- Rate limit: 60 requests/min allows time for deep analysis
- Maximize context processed per request
- Better to use full timeout window than risk incomplete analysis
- Large codebases benefit from thorough processing

**Results:** ✅ Success
# Usage in Claude Code


### Progress Monitoring

When the skill agent runs:
1. **Launch:** Gemini query starts in background
2. **Monitor:** Progress check every 60 seconds
3. **Update:** User sees "Analyzing... X min elapsed"
4. **Complete:** Results written to temp file
5. **Review:** User can inspect temp markdown before proceeding

**Temp File Format:**
```
C:\Users\<user>\AppData\Local\Temp\gemini_analysis_20251223_183045.md
```

**Benefits:**
- Non-blocking execution
- Progress visibility
- Manual review checkpoint
- Audit trail
# Progress Monitoring
When the skill agent runs:
1. **Launch:** Gemini query starts in background
2. **Monitor:** Progress check every 60 seconds
3. **Update:** User sees "Analyzing... X min elapsed"
4. **Complete:** Results written to temp file
5. **Review:** User validates findings in temp file
6. **Embed:** Content copied to permanent Obsidian doc
7. **Cleanup:** Temp file discarded after embedding

**Temp File Format:**
```
C:\Users\<user>\AppData\Local\Temp\gemini_analysis_20251223_183045.md
```

**Purpose:** Immediate validation during execution only.

**Permanent Storage:** Findings **embedded** in Obsidian vault under:
```
Vixen-Docs/Analysis/Gemini/YYYYMMDD_Topic.md
```

**Benefits:**
- Non-blocking execution
- Progress visibility
- Manual review checkpoint before committing
- Permanent documentation with embedded content
- No broken links to temp files
