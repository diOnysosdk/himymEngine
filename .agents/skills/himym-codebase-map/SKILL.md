---
name: himym-codebase-map
description: Orient within the HiMYM C++ demoscene repository before editing or reviewing. Use for architecture questions, unfamiliar subsystems, cue/data-flow tracing, ownership discovery, documentation reconciliation, or deciding which editor, runtime, graphics, audio, packing, and build files a change must touch.
---

# HiMYM codebase map

1. Read the root `AGENTS.md`.
2. Inspect current code before trusting field counts or APIs in older documentation.
3. Start from the shared contract:
   - cue structs: `revision_libs/rev_runtime/include/rev_runtime.h`
   - parsers: `revision_libs/rev_runtime/src/rev_runtime.cpp`
   - editor API: `revision_libs/rev_editor/include/rev_editor.h`
   - persistence/export/preview: `revision_libs/rev_editor/src/`
   - playback: `examples/minimal_intro/main.cpp`
   - packing: `revision_libs/rev_pack/` and `tools/pack_cli.cpp`
4. Trace behavior end-to-end rather than inferring it from one layer.
5. Use `PR/` and `.github/copilot/` as supporting context and note disagreements with code.

Read [references/contracts.md](references/contracts.md) for the detailed checklist.

Return a compact map naming authoritative types/functions, producers, consumers, validation targets, and documentation drift.
