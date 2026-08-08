# HiMYM Editor for Windows x64

HiMYM is a Windows demoscene editor for authoring shader, image, text, pixel,
particle, music, and glTF-based productions.

Created by Dennis "diOnysos/TiTAN/Equinox" Kjaer.

LinkedIn: <https://www.linkedin.com/in/dennis-kjaer-christensen/>

## Start the editor

Run:

```text
build\bin\Release\editor_app.exe
```

Keep the executable in this directory layout. The editor uses the directory
three levels above the executable as its workspace.

The editor executable is self-contained and does not require separate HiMYM
DLL files. Windows 11 x64 is the supported competition platform.

## Included Blender tool

The `blender` folder contains:

- `himym_blender.py`: the HiMYM Blender 5.2 sidebar and GLB export helper.
- `himym_blender_addon.zip`: the same Python tool packaged for Blender's
  install-from-disk workflow.
- `himym_template.blend`: a ready-to-use metric scene with the HiMYM root,
  camera, light, and scale reference.
- `BLENDER_GLTF_GUIDE.md`: the complete export, camera, procedural-material,
  and particle-attachment contract.

The most direct setup is to open `himym_blender.py` in Blender's Scripting
workspace and choose **Run Script**. The **HiMYM** panel then appears in the
3D View sidebar opened with `N`.

## Documentation

- `docs\EDITOR_GUIDE.md`: editor authoring workflow and controls.
- `blender\BLENDER_GLTF_GUIDE.md`: Blender and glTF workflow.

## Building standalone intros

This is the editor-only binary distribution. Editing, previewing, project
save/load, cue export, and asset packing are available. The editor's Build and
Pack/Build commands invoke CMake targets from a configured HiMYM source
workspace; they cannot compile a standalone intro from this binary-only bundle.

For compilation, copy `editor_app.exe` to the source workspace's
`build\bin\Release` directory or use the complete HiMYM source distribution,
configure it with CMake, and run the editor there.

## Files created while editing

The editor may create `imgui.ini`, project JSON, `cues.txt`, project asset
folders, pack caches, and project-local output directories. Store each
production in its own writable folder and keep its JSON, `cues.txt`, and
project-assets folder together when transferring it.

