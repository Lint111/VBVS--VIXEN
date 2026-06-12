# CLAUDE.md

<project id="vixen">
  <name>VIXEN</name>
  <description>Vulkan-based voxel rendering engine with SVO ray tracing</description>
  <stack>C++23, Vulkan 1.3, CMake, Windows/MSVC</stack>
</project>

---

<meta-rules id="rule-system">

  <instruction>
    Before EVERY response, load rules from the `project-rules` skill.
    Global rules (communication, engineering) come from `~/.claude/skills/project-rules/rules/`.
    VIXEN-specific rules come from `.claude/skills/project-rules/rules/`.
    Display loaded rules at START of response in code block.
  </instruction>

  <skill-reference>
    <skill>project-rules</skill>
    <location>.claude/skills/project-rules/</location>
  </skill-reference>

  <rule-files>
    <always description="Load every response">
      <file source="global">rules/communication.md</file>
      <file source="global">rules/engineering.md</file>
      <file source="local">rules/obsidian-first.md</file>
      <file source="local">rules/logging.md</file>
      <file source="local">rules/hacknplan-workflow.md</file>
    </always>
    <task-relevant description="Load when task matches">
      <file trigger="coding" source="local">rules/workflow.md</file>
      <file trigger="code-review" source="local">rules/code-review.md</file>
      <file trigger="building,testing" source="local">rules/commands.md</file>
      <file trigger="agent-launch" source="local">rules/agents.md</file>
      <file trigger="feature,complex-problem" source="local">rules/collaborative-development.md</file>
      <file trigger="python-sandbox,code-executor,word-doc,powerpoint" source="global">rules/code-executor.md</file>
    </task-relevant>
    <situational description="Load when condition arises">
      <file trigger="new-conversation" source="local">rules/session.md</file>
      <file trigger="build-error" source="local">rules/troubleshooting.md</file>
    </situational>
  </rule-files>

  <loading-protocol>
    <step order="1">Identify task type</step>
    <step order="2">Read ALWAYS rule files</step>
    <step order="3">Read task-relevant rule files</step>
    <step order="4">Display summary at response start</step>
  </loading-protocol>

  <display-format>
```rules
[ACTIVE RULES]
- communication: {from rules/communication.md}
- engineering: {from rules/engineering.md}
- obsidian-first: {from rules/obsidian-first.md}
- logging: {from rules/logging.md}
- hacknplan-workflow: Task tracking | Stage updates | Time logging
- [task-relevant]: {from loaded files}
```
  </display-format>

  <self-reference>
    This meta-rule applies to EVERY response.
    Rule loading is NOT optional.
  </self-reference>

</meta-rules>

---

<quick-reference>
  <build>cmake --build build --config Debug --parallel 16</build>
  <test>./build/libraries/SVO/tests/Debug/test_*.exe --gtest_brief=1</test>
  <docs>Vixen-Docs/ (Obsidian vault - search here first)</docs>
  <context>memory-bank/ (session state)</context>
  <index>DOCUMENTATION_INDEX.md (90+ docs)</index>
</quick-reference>

---

<project-structure>
  <directory name="libraries">Core libraries (SVO, RenderGraph, Profiler)</directory>
  <directory name="Vixen-Docs">Obsidian documentation vault</directory>
  <directory name="memory-bank">Session persistence files</directory>
  <directory name=".claude/skills">VIXEN-specific skill definitions including project-rules</directory>
  <directory name=".claude/agents">VIXEN-specific agent definitions</directory>
</project-structure>

---

<mcp-delegation id="context-optimization" priority="HIGH">

  <principle>
    MCP tools bloat conversation context. DELEGATE to specialized agents.
    Main conversation should NEVER directly invoke HacknPlan or Obsidian MCP tools.
  </principle>

  <delegation-table>
    | MCP Tool Prefix | Agent | Model | Purpose |
    |-----------------|-------|-------|---------|
    | `mcp__hacknplan__*` | hacknplan-manager | Sonnet | Tasks, sprints, time logging |
    | `mcp__obsidian-vault__*` | obsidian-manager | Sonnet | Vault docs, search |
    | `mcp__hacknplan-obsidian-glue__*` | obsidian-manager | Sonnet | Cross-references |
  </delegation-table>

  <workflow>
    1. User requests HacknPlan/Obsidian operation
    2. Launch appropriate agent via Task tool
    3. Agent handles MCP internally
    4. Report summary to user (not raw MCP output)
  </workflow>

  <example>
    User: "Log 2 hours on the shader task"
    Bad: Call mcp__hacknplan__log_work_session directly
    Good: Task(hacknplan-manager, "Log 2 hours on shader task")
  </example>

</mcp-delegation>
