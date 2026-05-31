---
applyTo: ".github/**/*.md"
description: "Use when editing workspace agents, instructions, skills, copilot-instructions, or customization routing so the specialization map stays coherent and discoverable."
---
# Agent Customization Instructions

## Focus
- Keep workspace customizations coherent as one system.
- When one domain changes, update the relevant agent, instruction, and skill together.
- Descriptions are the discovery surface; write them with concrete trigger phrases.

## Structure Rules
- Workspace-wide routing belongs in `.github/copilot-instructions.md` (auto-loaded by Copilot).
- Agents belong in `.github/copilot/agents/`.
- File instructions belong in `.github/copilot/instructions/` with narrow `applyTo` globs.
- Reusable project skills belong in `.github/copilot/skills/` — one folder per skill, each with a `SKILL.md`.
- `PR/ai/` is the maintained source for skills; sync updates to `.github/copilot/skills/` in the same pass.

## Skill Inventory
| Skill folder | Description |
|---|---|
| `himym-framework` | High-level framework overview, library APIs, integration patterns |
| `revision-codebase-map` | Project layout, struct relationships, all cue types, cues.txt format |
| `revision-runtime-core` | rev_runtime, minimal_intro, cue loaders, Mat4 math, packed build |
| `scene-block-editor` | rev_editor, cue authoring (image/text/music/mesh), export, pack-build-run |
| `revision-shader-authoring` | rev_shader, GLSL, Phong contract, wglGetProcAddress patterns |
| `revision-build-validation` | CMake build commands, rebuild targets, stale binary detection |
| `revision-director` | Cross-domain routing, new-cue-type pattern, coordination rules |

## Routing Rules
- Keep a clear split between: runtime work, editor/export work, shader work, mesh/graphics work, customization work.
- Cross-domain workflows route through `@director` or `Revision Director` skill.
- When adding a new cue type, all 7 steps of the canonical pattern must be followed (struct → parser → rev_editor.h → editor impl → ExportProject → preview render → runtime render).

## Quality Rules
- Avoid `applyTo: "**"` unless the instruction truly applies everywhere.
- Keep YAML frontmatter simple and valid.
- Prefer updating existing files over creating near-duplicates.
- Prefer skills for repeatable domain work.
- If a customization change codifies a durable workflow or a regression fix, also update `/memories/repo/`.

## Validation
- Verify skill descriptions still match their trigger domain.
- Verify agent applyTo globs cover the right files.
- Keep `.github/copilot-instructions.md` and `.github/copilot/README.md` synchronized with the actual skill/agent inventory.
