# dicTerm Agent System

This project uses **opencode** with a multi-agent architecture.
Agents are defined in `.opencode/agents/` and describe reusable specialist roles
that the orchestrator (primary agent) delegates work to.

## Agent roles

| Agent | Type | Scope of work |
|-------|------|--------------|
| **orchestrator** | primary | Lead engineer. Breaks features into tasks, spawns subagents in parallel, merges results, runs tests, requests review. |
| **ansi** | subagent | ANSI escape sequence parsing: CSI, OSC, SGR, cursor movement, terminal state. |
| **font-renderer** | subagent | Font loading, glyph atlases, text shaping, glyph caching. |
| **input** | subagent | Keyboard and mouse handling, clipboard, scrolling, shortcuts. |
| **renderer** | subagent | GPU rendering: OpenGL/Vulkan, batching, vertex buffers, shaders. |
| **reviewer** | subagent | Code review: bugs, race conditions, architecture, style, performance. Read-only (no edit permission). |

## How agents work together

1. The **orchestrator** receives a feature request.
2. It decomposes the request into independent tasks.
3. It spawns the relevant subagents in parallel (e.g., `ansi` for the parser,
   `input` for keyboard handling, `renderer` for GPU work).
4. It waits for all subagents to finish, then merges their output.
5. It runs the test suite and fixes any regressions.
6. It asks the **reviewer** to audit the final implementation.
7. It produces a concise summary and commits.

## Adding a new agent

Create a markdown file in `.opencode/agents/<name>.md` with front matter:

```yaml
---
description: What this agent does.
mode: subagent
hidden: true
---
```

Then grant the orchestrator permission to spawn it by adding an entry in
`orchestrator.md`:

```yaml
permission:
  task:
    "<name>": allow
```

The agent body should describe exactly what the agent is allowed to touch
(e.g., "You only work on GPU rendering").  Agents are expected to stay within
their scope and never modify unrelated code.

## Running the agents

Agents are invoked automatically by opencode when a task matches their
description.  You can also invoke them explicitly via the `task` tool with
`subagent_type` set to the agent name (e.g., `ansi`).
