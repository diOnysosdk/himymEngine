---
name: himym-build-validation
description: Configure, build, test, and diagnose HiMYM changes. Use after C++, CMake, shader, editor, runtime, packer, text-animation, audio, mesh, pixel, or particle changes; when checking stale binaries; or when selecting proportional Release and optional-3D validation targets.
---

# HiMYM build validation

1. Inspect `git status` and the affected dependency chain.
2. Prefer the existing `build` tree and a focused Release target.
3. Reconfigure only when CMake inputs/options changed or the tree is incompatible.
4. Build direct consumers after static-library changes.
5. Run focused tests or smoke paths.
6. For packer changes, rebuild the tool/editor before generating assets, then rebuild `minimal_intro_packed`.
7. Perform launch/visual checks when the environment permits.
8. Report commands, results, and whether failures predate the change.

Use commands in root `AGENTS.md`. Do not delete build directories or regenerate unrelated outputs merely to get a clean result.
