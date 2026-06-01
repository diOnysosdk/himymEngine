# GitHub Copilot Agent System for HiMYM

Copilot customizations for the HiMYM C++17 demoscene framework.

## Directory Structure

```
.github/
├── copilot-instructions.md            # Workspace routing (auto-loaded by Copilot)
└── copilot/
    ├── README.md                      # This file
    ├── agents/                        # Specialist agents
    │   ├── director.agent.md          # Routing + cross-domain orchestration
    │   ├── runtime-dev.agent.md       # rev_runtime, minimal_intro, cue loaders, Mat4
    │   ├── editor-dev.agent.md        # rev_editor, cue authoring, export, pack-build-run
    │   ├── mesh-graphics.agent.md     # rev_mesh, MeshCue, Phong shader, 3D geometry
    │   ├── shader-author.agent.md     # GLSL, rev_shader, shader contracts
    │   ├── build-system.agent.md      # CMake, build flags, target linking
    │   ├── music-system.agent.md      # rev_xm, XM playback, WinMM audio
    │   └── docs.agent.md              # README, architecture, API docs
    ├── instructions/                  # File-pattern-scoped instructions
    │   ├── coding-standards.instructions.md      # C++17 conventions
    │   ├── size-optimization.instructions.md     # Size discipline
    │   ├── opengl-patterns.instructions.md       # OpenGL loading patterns
    │   └── agent-customizations.instructions.md  # Customization system rules
    └── skills/                        # Reusable knowledge bases
        ├── himym-framework/SKILL.md   # Framework overview, all library APIs
        ├── revision-codebase-map/SKILL.md        # Layout, structs, cues.txt format
        ├── revision-runtime-core/SKILL.md        # rev_runtime, minimal_intro, Mat4
        ├── scene-block-editor/SKILL.md           # rev_editor, cue authoring/export
        ├── revision-shader-authoring/SKILL.md    # rev_shader, Phong, wglGetProcAddress
        ├── revision-build-validation/SKILL.md    # CMake commands, rebuild targets
        └── revision-director/SKILL.md            # Cross-domain routing rules
```

## Agents

| Agent | Scope |
|-------|-------|
| `@director` | Mixed/cross-domain tasks — routes to specialists, applies new-cue-type pattern |
| `@runtime-dev` | `rev_runtime/`, `minimal_intro/` — cue structs, parsers, Mat4, packed build |
| `@editor-dev` | `rev_editor/`, `editor_app/` — cue authoring, export, mesh modal, pack-build-run |
| `@mesh-graphics` | `rev_mesh/` — procedural geometry, MeshCue render integration |
| `@shader-author` | `rev_shader/`, `.glsl` — shader compilation, Phong contract |
| `@build-system` | `CMakeLists.txt` — cmake config, target linking, build flags |
| `@music-system` | `rev_xm/` — XM playback, WinMM audio thread |
| `@docs` | `**/*.md` — documentation |

## Skills

Load these by name when relevant context is needed:

| Skill | Use for |
|-------|---------|
| `HiMYM Framework` | General framework overview, library APIs |
| `Revision Codebase Map` | Layout, all struct relationships, cues.txt format (all 4 sections) |
| `Revision Runtime Core` | rev_runtime, minimal_intro render loop, Mat4, MeshCue fields |
| `Scene Block Editor` | rev_editor, mesh cue authoring/export, pack-build-run |
| `Shader Authoring` | GLSL, Phong contract, rev_shader API, wglGetProcAddress |
| `Revision Build Validation` | CMake commands, rebuild targets, stale binary detection |
| `Revision Director` | Cross-domain coordination, new-cue-type pattern |

## Cue System Quick Reference

All cue structs live in `rev_runtime.h`. Never redefine elsewhere.

| Type | Base Fields | With Curves | cues.txt section |
|------|-------------|-------------|-----------------|
| ShaderCue | 25 | 42 (17 curve indices) | `[shader_cues]` |
| ImageCue | 14 | 18 (4 curve indices) | `[image_cues]` |
| TextCue | 16 | 22 (6 curve indices) | `[text_cues]` |
| MusicCue | 4 | 4 (no curves) | `[music_cues]` |
| MeshCue | 28 | 44 (16 curve indices) | `[mesh_cues]` |

**Curve animation**: All animatable parameters can reference curves via `int curve_*` fields (-1 = no curve, 0-31 = curve index). Runtime evaluates curves at `(elapsed_time / curve.duration)` and uses animated values for uniforms/transforms.

**Auto-save pattern**: All editor modals auto-save changes immediately (no Apply/Cancel workflow). Every UI control change triggers `AutoSave()` lambda that copies `editing_cue` to `scene->cues[index]`.

**New cue type pattern**: struct in `rev_runtime.h` → parser in `rev_runtime.cpp` → `rev_editor.h` using → SceneBlock/EditorContext fields → Add/Delete/Modal/UI/Export/Load in `editor_context.cpp` → `RenderPreviewFrame` → `minimal_intro/main.cpp` → rebuild both targets.

## Framework Libraries (8)

| Library | Purpose |
|---------|---------|
| `rev_runtime` | Shared cue structs, parsers, GDI+ helpers, Mat4 math (source of truth) |
| `rev_platform` | Win32 window, OpenGL context |
| `rev_shader` | GLSL shader compile/link |
| `rev_xm` | XM/MOD music (libxm-windows, C89) |
| `rev_curve` | Bézier curve evaluation |
| `rev_sequence` | Timeline sequencing |
| `rev_editor` | ImGui visual authoring |
| `rev_mesh` | Procedural 3D geometry (cube/sphere/plane/torus), VAO/VBO/IBO |

## Maintenance

- **`PR/ai/skills/`** is the maintained source; sync to `.github/copilot/skills/` each session.
- When a domain changes, update: agent description + relevant skill + `copilot-instructions.md`.
- `agent-customizations.instructions.md` tracks the full skill/agent inventory.
