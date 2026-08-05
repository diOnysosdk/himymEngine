"""Headless smoke fixture for the HiMYM Blender attachment exporter."""

import importlib.util
import os
import sys

import bpy


def load_himym_tools():
    path = os.path.abspath("tools/blender/himym_blender.py")
    spec = importlib.util.spec_from_file_location("himym_blender_tools", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    output = os.path.abspath(
        sys.argv[sys.argv.index("--") + 1]
        if "--" in sys.argv else "build/blender_attachment_test.glb"
    )
    tools = load_himym_tools()
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    root, collection = tools.ensure_himym_scene()

    bpy.ops.mesh.primitive_cube_add(size=1.0)
    mesh = bpy.context.object
    mesh.name = "AttachmentFixtureMesh"
    mesh.parent = root

    socket = bpy.data.objects.new("FX_Test", None)
    collection.objects.link(socket)
    socket.parent = mesh
    socket.location = (0.25, 0.5, 0.75)
    tools.mark_attachment(socket, "FX_Test", "-Y")
    warnings = tools.validate_scene()
    duplicate_warnings = [warning for warning in warnings if "Duplicate" in warning]
    if duplicate_warnings:
        raise RuntimeError("; ".join(duplicate_warnings))
    tools.export_glb(output)
    print(f"[blender_attachment_export_test] PASS {output}")


if __name__ == "__main__":
    main()
