# Prompt Parity Notes (HiMYM)

This folder stores reusable prompt entry points for recurring tasks.

Current policy:

- Skills are the primary source of domain behavior.
- Prompts should stay thin and route to the correct skill/agent.
- When runtime rendering contracts change, update matching prompt guidance in the same pass as:
  - `PR/architecture/ARCHITECTURE.md`
  - `PR/architecture/API-REFERENCE.md`
  - `PR/context/CODE_STYLE.md`
  - Relevant skill files under `PR/ai/skills/`

Runtime rendering contract highlights to keep in prompt guidance:

- Imported glTF meshes can contain mixed textured and color-only material slots.
- Mesh alpha behavior must include sampled texture alpha.
- Opaque slots render before transparent slots for imported mixed-material meshes.
- Runtime/editor rendering semantics should remain aligned.
