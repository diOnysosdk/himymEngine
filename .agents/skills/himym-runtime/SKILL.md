---
name: himym-runtime
description: Implement or review HiMYM playback/runtime behavior. Use for shared cue structs and parsers, minimal_intro, packed assets, scene wipes, interactive menu navigation, Win32/WGL frame flow, GDI+ image/text loading, XM/WinMM playback, curves, triggers, runtime layering, timing, cursor lifecycle, or size-sensitive runtime fixes.
---

# HiMYM runtime

Read `AGENTS.md` and use `$himym-codebase-map` when ownership is unclear.

1. Identify the authored contract and every producer/consumer.
2. Change shared cue layouts and parsing in `rev_runtime` first.
3. Keep editor persistence/export and preview aligned whenever semantics change.
4. Preserve explicit frame order and resource ownership.
5. Keep release behavior deterministic and packed-asset friendly.
6. Validate the smallest affected target, then packed/editor targets for cross-layer changes.

For optional competition builds, preserve the normal x64 artifact first. Treat
Crinkler as an x86 replacement linker: WGL-loaded function pointers require
`APIENTRY`, all linked objects must be ordinary COFF rather than LTCG, pragma-
only default libraries must be made explicit, and reuse layouts must be
regenerated when packed manifests change.

Do not duplicate shared types or texture helpers. Preserve `curve_* == -1`, GDI+ requirements, modern GL loading through `wglGetProcAddress`, GL state restoration, and cue-driven XM startup. Avoid speculative abstractions and report meaningful binary/dependency costs.

For interactive navigation, preserve the originating menu as explicit session
state. Menu scenes loop until activation or exit; destinations return to that
origin on authored scene expiry or non-looping XM completion. Treat sprite
menu visuals as instances of scene-local animated-sprite cues with menu-owned
position, hit bounds, target, and selection tint.
Keep a separate animation clock per sprite menu item. Advance only the selected
item, freeze it when selection leaves, and resume its stored clock on re-entry;
do not derive menu-instance frames from the looping scene clock.
Gate hover and click handling on the authored per-menu mouse option. Keep that
option disabled by default and do not gate keyboard navigation with it.
