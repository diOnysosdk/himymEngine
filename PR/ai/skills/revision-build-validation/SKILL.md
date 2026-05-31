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
cmake --build build --config Release --target editor_app minimal_intro
```

## IMPORTANT: run cmake from workspace root
Always run cmake from `E:\code\cpp\mono\himym` — do NOT cd into build/bin/Release first, or cmake will fail with "not a directory" error.

## Debug image loading
- Check `build/bin/Release/intro_debug.log` after running `minimal_intro.exe`
- GDI+ error 3 = FileNotFound OR GDI+ not initialized (check both)
- `LoadImageCue` returns false = cues.txt parser field mismatch (check field count)

## Validation rules
- Prefer the narrowest command that can falsify the current hypothesis.
- Do not widen the validation scope before the first focused check.
- Treat build results as part of the implementation, not a postscript.
- After changing cues.txt export format, always verify `LoadImageCue` parser field count matches.