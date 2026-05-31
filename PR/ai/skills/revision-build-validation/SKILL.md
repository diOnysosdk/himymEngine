---
name: Revision Build Validation
description: "Use for the build and compile checks that verify C++ runtime, editor, shader, and customization changes."
---
# Revision Build Validation

Use this skill to validate changes with the smallest useful command set.

## Default checks
```powershell
cmake --build build --config Release
```

## Targeted rebuilds (faster)
```powershell
cmake --build build --config Release --target editor_app
cmake --build build --config Release --target minimal_intro
cmake --build build --config Release --target minimal_intro_packed
cmake --build build --config Release --target rev_runtime
cmake --build build --config Release --target editor_app minimal_intro
```

## Reconfigure (CMakeLists.txt changed)
```powershell
cmake -B build -S .
cmake --build build --config Release
```

## IMPORTANT: run cmake from workspace root
Always run cmake from `E:\code\cpp\mono\himym` — do NOT cd into build/bin/Release first, or cmake will fail with "not a directory" error.

## Stale editor binary detection
After any change to `revision_libs/rev_pack/src/rev_pack.cpp`, the editor binary (which statically links rev_pack) must be rebuilt **before** running "Pack, Build and Run":
```powershell
cmake --build build --config Release --target editor_app
```
To verify the resulting `packed_assets.h` is fresh:
```powershell
Select-String "HIMYM_HAS_PACKED_CUES" build/packed_assets.h
```
If this returns nothing, the editor binary is stale — rebuild `editor_app` and re-run Pack.

## PRE_BUILD touch rule
`examples/minimal_intro/CMakeLists.txt` has a PRE_BUILD command that touches `main.cpp` before every `minimal_intro_packed` build. This forces MSBuild to recompile `main.cpp` and pick up the latest `packed_assets.h`. Do not remove this rule — `OBJECT_DEPENDS` is unreliable with Visual Studio generators.

## Debug image loading
- Check `build/bin/Release/intro_debug.log` after running `minimal_intro.exe`
- GDI+ error 3 = FileNotFound OR GDI+ not initialized (check both)
- `LoadImageCue` returns false = cues.txt parser field mismatch (check field count)

## Validation rules
- Prefer the narrowest command that can falsify the current hypothesis.
- Do not widen the validation scope before the first focused check.
- Treat build results as part of the implementation, not a postscript.
- After changing cues.txt export format, always verify `LoadImageCue` and `LoadTextCue` parser field counts in `rev_runtime.cpp` match.
- After changing `rev_pack.cpp`, rebuild `editor_app` before re-packing.
- After changing `rev_runtime.h` or `rev_runtime.cpp`, rebuild both `editor_app` and `minimal_intro` (both link rev_runtime).