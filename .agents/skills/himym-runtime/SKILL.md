---
name: himym-runtime
description: Implement or review HiMYM playback/runtime behavior. Use for shared cue structs and parsers, minimal_intro, packed assets, Win32/WGL frame flow, GDI+ image/text loading, XM/WinMM playback, curves, triggers, runtime layering, timing, cursor lifecycle, or size-sensitive runtime fixes.
---

# HiMYM runtime

Read `AGENTS.md` and use `$himym-codebase-map` when ownership is unclear.

1. Identify the authored contract and every producer/consumer.
2. Change shared cue layouts and parsing in `rev_runtime` first.
3. Keep editor persistence/export and preview aligned whenever semantics change.
4. Preserve explicit frame order and resource ownership.
5. Keep release behavior deterministic and packed-asset friendly.
6. Validate the smallest affected target, then packed/editor targets for cross-layer changes.

Do not duplicate shared types or texture helpers. Preserve `curve_* == -1`, GDI+ requirements, modern GL loading through `wglGetProcAddress`, GL state restoration, and cue-driven XM startup. Avoid speculative abstractions and report meaningful binary/dependency costs.
