# Documentation Authority and Relevance

This repository contains documentation from several generations of HiMYM. Do not assume a document is current merely because it is under `PR/`.

## Authority order

When sources disagree, use this order:

1. Current C++ headers, implementations, tests, and CMake target definitions.
2. Root `AGENTS.md` for repository-wide engineering rules.
3. Documents marked **Current** below.
4. Documents marked **Mixed**, after checking each claim against code.
5. **Historical** and **Legacy** material only for design intent, migration context, or archaeology.

Dates and feature lists are not proof of authority. In particular, old cue field counts and library inventories frequently lag the implementation.

## Current

These are the preferred documentation entry points. They can still drift, so verify exact APIs and serialization formats in code.

| Document | Use |
|---|---|
| `../README.md` | Current high-level C++ framework overview and repository entry point. |
| `../BUILD.md` | Current build and authoring-handoff workflow. |
| `architecture/ARCHITECTURE.md` | Current architectural concepts and cross-layer contracts. |
| `architecture/API-REFERENCE.md` | Preferred API/format reference; verify exact signatures and field counts in headers/parsers. |
| `context/CODE_STYLE.md` | Current coding, build-profile, and completion rules. |
| `guides/EDITOR_GUIDE.md` | Current C++/ImGui editor workflow. |
| `CURVES_RUNTIME_USAGE.md` | Current curve consumption patterns. |
| `QUICK_START.md` | Useful current quick-start material, subject to code/build verification. |

For Codex behavior, use the root `AGENTS.md`, `.agents/skills/`, and `.codex/agents/`.

## Mixed: useful but verify

These contain useful principles or subsystem explanations alongside outdated layouts, names, feature inventories, or workflows.

| Document | Keep | Verify or ignore |
|---|---|---|
| `architecture/API_REFERENCE.md` | Additional API notes. | Duplicate of the hyphenated API reference; prefer `API-REFERENCE.md`. |
| `architecture/TECH-STACK.md` | Platform/dependency background. | Current library list, versions, profiles, and asset pipeline. |
| `guides/MESH_GUIDE.md` | Mesh/glTF authoring concepts. | Current material-slot, transparency, animation, and import behavior. |
| `guides/SHADER_GUIDE.md` | GLSL patterns and shader-authoring ideas. | Python-editor references, tool availability, paths, uniforms, and dispatch details. |
| `guides/CONTROLS_KNOBS.md` | Naming philosophy and some timing concepts. | Old runtime layout, retired features, defaults, and scene-specific knobs. |
| `context/PROJECT_GUIDELINES.md` | Size discipline, deterministic flow, Windows/compo priorities. | Its older `src/` layout and production-specific contracts. Root `AGENTS.md` wins. |
| `context/OPENGL-EXPLAINER.md` | WGL/OpenGL background and common constraints. | Exact current renderer structure and shader pipeline. |
| `context/EXTENSION-GUIDE.md` | Cross-layer thinking. | File paths, supported cue types, and step counts. |
| `tools/SHADER_TOOLS.md` | Historical tool intent. | Confirm every referenced script exists before using it. |

The `.github/copilot/` tree is live configuration for GitHub Copilot, not Codex. It may provide domain context, but its APIs, counts, and inventories must be checked against code. Do not copy its agent format into Codex configuration.

## Historical design and migration material

Use these to understand how the current all-C++ architecture evolved. Do not implement directly from their APIs, size estimates, schedules, or completion claims.

| Document | Historical purpose |
|---|---|
| `FROM_SCRATCH_V2.md` | Proposed all-C++ modular architecture that led toward the current system. |
| `LIBRARY_DESIGN.md` | Early library/API design specification. |
| `ROADMAP.md` | Initial staged implementation plan. |
| `REFACTORING_NOTES.md` | Python-to-C++ migration rationale. |
| `GETTING_STARTED_NEW_PROJECT.md` | Early project-bootstrap workflow. |
| `EDITOR_VISION.md` | Early editor feature vision and backlog. |
| `../PHASE1_COMPLETE.md` | Phase-one milestone snapshot. |

## Legacy Python/editor documentation

These describe the older Python/tkinter generation or retain substantial Python-era assumptions. They are not instructions for the current C++/ImGui editor.

| Document or tree | Status |
|---|---|
| `README.md` | Legacy multi-generation documentation index; retained as an archive entry point. |
| `SUMMARY.md` | Legacy inventory and completion summary. |
| `FROM_SCRATCH.md` | Mixed early design with substantial Python-editor implementation material. |
| `ai/agents/scene-block-editor.agent.md` | Python/tkinter specialist definition. |
| `ai/instructions/scene_block_editor.instructions.md` | Python editor instructions and validation commands. |
| `ai/skills/python-editor-tooling-map/` | Python editor ownership map. |
| `ai/skills/python-editor-utilities/` | Python helper workflows. |
| `ai/prompts/` | Legacy prompt wrappers; Codex uses repository skills instead. |

Other files under `PR/ai/` are snapshots or source material for older agent systems. They are not auto-loaded by Codex and must not override the root `AGENTS.md`.

## Maintenance rule

When changing a durable contract:

1. Update code and focused tests first.
2. Update the relevant **Current** document.
3. Update this status map if a document becomes authoritative, mixed, historical, or retired.
4. Add a visible warning to misleading legacy entry points rather than silently deleting useful project history.

If a document cannot be verified during a change, label the uncertain statement instead of presenting it as current fact.
