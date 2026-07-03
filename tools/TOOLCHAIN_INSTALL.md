# HiMYM Toolchain Installer

Use this when sharing the editor with someone who needs to build projects locally.

## Files

- `install_himym_toolchain.cmd` (double-click friendly)
- `install_himym_toolchain.ps1` (script implementation)
- `install_and_prepare_local_build.cmd` (installs toolchain + generates fresh local `build/` + builds Release)

## What it installs

- Visual Studio 2022 Build Tools
- MSVC v143 C++ toolchain
- MSBuild
- CMake integration component
- Windows 11 SDK (10.0.22621)
- CMake (if missing)

## How your friend should use it

1. Easiest: run `install_and_prepare_local_build.cmd`.
2. Alternative: run `install_himym_toolchain.cmd` and do configure/build manually.
2. Accept elevation (UAC prompt) if requested.
3. Wait for install to complete.
4. Open a new terminal and run:

   `cmake -S . -B build -G "Visual Studio 17 2022"`

   `cmake --build build --config Release`

## Notes

- Install location can be any disk (C:, D:, E:, etc.).
- The script clears stale user SDK env vars (`WindowsSdkDir`, `WindowsSDKVersion`) that can break CMake/MSBuild detection.
- Build sharing guard is automated in bootstrap mode: existing `build/CMakeCache.txt` and `build/CMakeFiles/` are removed before configure.
- Do not share your `build/` folder between machines. Each machine should configure/build locally.

## Optional PowerShell flags

- `-PrepareLocalBuild`: remove stale cache and run fresh `cmake -S .. -B ../build -G "Visual Studio 17 2022"`
- `-BuildRelease`: when used with `-PrepareLocalBuild`, also run `cmake --build ../build --config Release`
- `-SkipCMake`: skip CMake installation if you only want Build Tools
