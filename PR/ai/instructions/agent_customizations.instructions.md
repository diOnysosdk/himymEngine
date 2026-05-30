---
applyTo: .github/**/*.md
description: "Use when editing workspace agents, instructions, prompts, copilot-instructions, or customization routing so the specialization map stays coherent and discoverable."
---
# Agent Customization Instructions

## Focus
- Keep workspace customizations coherent as one system.
- When one domain changes, update the relevant agent, instruction, and prompt together when needed.
- Descriptions are the discovery surface; write them with concrete trigger phrases.

## Structure Rules
- Agents belong in `.github/agents/`.
- File instructions belong in `.github/instructions/` with narrow `applyTo` globs.
- Reusable project skills belong in `.github/skills/` and should describe a composable capability, not a one-off task prompt.
- Reusable user-facing task templates belong in `.github/prompts/`.
- Workspace-wide routing or policy belongs in `.github/copilot-instructions.md` or `AGENTS.md`.

## Routing Rules
- Keep a clear split between:
  - intro runtime work
  - scene editor/export work
  - shader authoring work
  - customization/routing work
- Add Python editor tooling skills when a helper script deserves a stable reuse surface distinct from the main editor UI.
- Cross-domain workflows should have an orchestration path rather than duplicating policy in every specialist.

## Quality Rules
- Avoid `applyTo: "**"` unless the instruction truly applies everywhere.
- Keep YAML frontmatter simple and valid.
- Prefer updating existing customizations over creating near-duplicates.
- Prefer skills for repeatable domain work; keep prompts thin and optional.
- If a customization change codifies a durable workflow or a regression fix, keep `/memories/repo/` in sync with the verified note.

## Validation
- Verify descriptions still match their trigger domain.
- Verify new filenames and names are stable and discoverable.
- Keep references to existing agents/prompts synchronized.