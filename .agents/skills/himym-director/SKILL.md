---
name: himym-director
description: Coordinate HiMYM tasks spanning two or more domains such as runtime contracts, editor export, standalone rendering, shaders/graphics, packing, documentation, and validation. Use for new cue types, cross-layer semantics, broad refactors, or explicitly requested parallel agent work.
---

# HiMYM director

1. Read `AGENTS.md` and invoke `$himym-codebase-map`.
2. Define one end-to-end contract before splitting work.
3. Route work through `$himym-runtime`, `$himym-editor`, `$himym-shader-graphics`, and `$himym-build-validation`.
4. Keep struct/parser/export/render changes in coherent order.
5. If the user explicitly requests subagents, use `.codex/agents/`; parallelize read-only work and serialize overlapping writes.
6. Integrate against current code, update durable docs, and validate.

When competition output crosses build and graphics domains, keep one explicit
contract: preserve normal x64 first; build Crinkler separately as x86 from
non-LTCG COFF with explicit imports; fingerprint reuse to packed manifests;
require `APIENTRY` on every WGL-loaded function pointer; and validate actual
x86 playback rather than accepting a successful link as rendering evidence.

Return one outcome summary with changed contracts, affected layers, evidence, and remaining risks.
