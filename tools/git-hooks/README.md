# Git hooks

Tracked hooks for this repository. Git does not run hooks from a tracked
directory by default; enable them once per clone with:

    git config core.hooksPath tools/git-hooks

| Hook | What it does |
|---|---|
| `commit-msg` | Rejects any commit message mentioning Claude / Anthropic / AI assistants, including Co-Authored-By trailers. Standing project rule; see CLAUDE.md. |
