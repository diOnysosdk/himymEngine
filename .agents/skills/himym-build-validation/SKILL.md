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

For Crinkler work, validate that the normal artifact is PE32+ x64 and preserved,
the competition artifact is PE32 x86, dependencies do not emit `/GL`/LTCG
objects, pragma-only system libraries are explicit, and the reuse fingerprint
tracks both packed manifests. A successful link is insufficient for graphics
changes: smoke-test repeated WGL calls in the x86 executable because missing
`APIENTRY` annotations are invisible on x64 and typically fail as flicker,
black output, or a delayed crash.

Exercise the selected 64 KiB, 128 KiB, or custom final-size budget independently
from the per-part 64 KiB reuse constraint. If the competition output is running,
expect the wrapper to fail before linking with a close-the-process diagnostic.

Use commands in root `AGENTS.md`. Do not delete build directories or regenerate unrelated outputs merely to get a clean result.
