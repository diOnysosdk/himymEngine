---
name: himym-editor
description: Implement or review the HiMYM ImGui editor and authoring pipeline. Use for project JSON, scene/cue modals, scene wipes, interactive menus, timeline and curve UI, preview rendering, asset importing/copying, cues.txt export/import, project packing, Do It All, editor reliability, or authored data that must match runtime consumption.
---

# HiMYM editor

Read `AGENTS.md`, shared cue definitions, `rev_editor.h`, and the relevant editor sources.

1. Identify the runtime contract before changing editor-owned data.
2. Preserve create/load/save/import/export round trips.
3. Keep modal edits, selection state, ownership, and modified state explicit.
4. Keep preview evaluation equivalent to standalone runtime behavior.
5. Preserve deterministic export and project-relative asset paths.
6. Update parsers/rendering when exported semantics change.
7. Build `himym_editor`; test headless or round-trip paths when affected.

Do not redefine shared cues. Initialize new fields deliberately, preserve copied-asset cleanup coverage, keep save -> export -> pack -> build -> launch explicit, and avoid generalized UI frameworks or new dependencies.

For menu authoring, keep ImGui IDs stable while labels are edited. Preserve
scene-local animated-sprite references across save/load/export/import and clear
or reindex them when cues are deleted. Menu-owned image position and hit bounds
must remain independent for every item, including items sharing one sprite cue.
