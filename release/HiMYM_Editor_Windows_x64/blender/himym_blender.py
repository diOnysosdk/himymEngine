"""HiMYM Blender 5.2 scene setup and glTF export helper.

Interactive:
  Open this file in Blender's Scripting workspace and press Run Script.
  The "HiMYM" panel appears in the 3D View sidebar.

Command line:
  blender --background file.blend --python himym_blender.py -- --export out.glb
  blender --background --factory-startup --python himym_blender.py -- --template out.blend
"""

bl_info = {
    "name": "HiMYM glTF Tools",
    "author": "Dennis \"diOnysos/TiTAN/Equinox\" Kjaer",
    "version": (1, 0, 0),
    "blender": (5, 2, 0),
    "location": "View3D > Sidebar > HiMYM",
    "description": "Set up and export compact Blender scenes for HiMYM",
    "category": "Import-Export",
}

import argparse
import math
import os
import sys

import bpy
from mathutils import Vector


ROOT_NAME = "HIMYM_ROOT"
COLLECTION_NAME = "HiMYM"
ATTACHMENT_PREFIX = "FX_"


def _link_object(obj, collection):
    for owner in list(obj.users_collection):
        owner.objects.unlink(obj)
    collection.objects.link(obj)


def _look_at(obj, target):
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat("-Z", "Y").to_euler()


def ensure_himym_scene():
    """Apply non-destructive scene defaults and ensure an identity export root."""
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0

    collection = bpy.data.collections.get(COLLECTION_NAME)
    if collection is None:
        collection = bpy.data.collections.new(COLLECTION_NAME)
        scene.collection.children.link(collection)

    root = bpy.data.objects.get(ROOT_NAME)
    if root is None:
        root = bpy.data.objects.new(ROOT_NAME, None)
        collection.objects.link(root)
    root.empty_display_type = "PLAIN_AXES"
    root.empty_display_size = 1.0
    root.location = (0.0, 0.0, 0.0)
    root.rotation_euler = (0.0, 0.0, 0.0)
    root.scale = (1.0, 1.0, 1.0)
    return root, collection


def sync_camera_properties():
    """Copy Blender lens shift to the compact HiMYM camera extras contract."""
    for camera in bpy.data.cameras:
        camera["himym_camera_shift_x"] = float(camera.shift_x)
        camera["himym_camera_shift_y"] = float(camera.shift_y)


def _socket_value(node, name, default):
    socket = node.inputs.get(name)
    return float(socket.default_value) if socket is not None else default


def _infer_noise_target(node):
    for output in node.outputs:
        for link in output.links:
            socket_name = link.to_socket.name.lower()
            if socket_name == "roughness":
                return "roughness"
            if socket_name in {"base color", "base_color"}:
                return "base_color"
            if socket_name in {"emission color", "emission"}:
                return "emission"
    return None


def sync_material_noise():
    """Export a deliberately named Blender Noise Texture as HiMYM extras.

    Label a Noise Texture node exactly "HiMYM Noise". Direct links to Principled
    Base Color, Roughness, or Emission infer the target. A material custom
    property named himym_noise_target overrides inference.
    """
    for material in bpy.data.materials:
        if not material.use_nodes or not material.node_tree:
            continue
        noise = next(
            (
                node
                for node in material.node_tree.nodes
                if node.bl_idname == "ShaderNodeTexNoise"
                and (node.label == "HiMYM Noise" or node.name == "HiMYM Noise")
            ),
            None,
        )
        if noise is None:
            continue
        target = material.get("himym_noise_target") or _infer_noise_target(noise)
        if target not in {"base_color", "roughness", "emission", 1, 2, 3}:
            print(
                f"[HiMYM] Material '{material.name}': label the direct Noise Texture "
                "link or set himym_noise_target; procedural noise was not exported."
            )
            continue
        material["himym_noise_target"] = target
        material["himym_noise_scale"] = _socket_value(noise, "Scale", 5.0)
        material["himym_noise_detail"] = _socket_value(noise, "Detail", 2.0)
        material["himym_noise_roughness"] = _socket_value(noise, "Roughness", 0.5)
        material["himym_noise_distortion"] = _socket_value(noise, "Distortion", 0.0)
        if "himym_noise_strength" not in material:
            material["himym_noise_strength"] = 1.0


def attachment_objects():
    return [
        obj for obj in bpy.context.scene.objects
        if bool(obj.get("himym_attachment", False))
    ]


def mark_attachment(obj, name=None, axis="+Z"):
    if obj is None:
        raise RuntimeError("Select an object or create an attachment first.")
    obj["himym_attachment"] = True
    obj["himym_attachment_name"] = name or obj.name
    obj["himym_direction_axis"] = axis if axis in {"+X", "-X", "+Y", "-Y", "+Z", "-Z"} else "+Z"
    return obj


def validate_scene():
    warnings = []
    root = bpy.data.objects.get(ROOT_NAME)
    if root is None:
        warnings.append(f"Missing {ROOT_NAME}; run Setup Scene.")
    elif (
        root.location.length > 1.0e-6
        or any(abs(angle) > 1.0e-6 for angle in root.rotation_euler)
        or any(abs(value - 1.0) > 1.0e-6 for value in root.scale)
    ):
        warnings.append(f"{ROOT_NAME} must remain at identity transform.")

    cameras = [obj for obj in bpy.context.scene.objects if obj.type == "CAMERA" and not obj.hide_render]
    if not cameras:
        warnings.append("No renderable camera will be exported.")
    elif len(cameras) > 1:
        warnings.append("More than one camera is renderable; HiMYM currently uses the first glTF camera.")

    for obj in bpy.context.scene.objects:
        if obj.type == "MESH" and any(abs(value - 1.0) > 1.0e-5 for value in obj.scale):
            warnings.append(f"Mesh '{obj.name}' has unapplied scale (supported, but apply it when practical).")
    attachment_names = set()
    for obj in attachment_objects():
        name = str(obj.get("himym_attachment_name", "")).strip()
        axis = str(obj.get("himym_direction_axis", "+Z"))
        if not name:
            warnings.append(f"Attachment object '{obj.name}' has no attachment name.")
        elif name in attachment_names:
            warnings.append(f"Duplicate HiMYM attachment name '{name}'.")
        elif "|" in name:
            warnings.append(f"Attachment name '{name}' cannot contain '|'.")
        attachment_names.add(name)
        if axis not in {"+X", "-X", "+Y", "-Y", "+Z", "-Z"}:
            warnings.append(f"Attachment '{name or obj.name}' has invalid direction axis '{axis}'.")
    return warnings


def export_glb(filepath):
    ensure_himym_scene()
    sync_camera_properties()
    sync_material_noise()
    for warning in validate_scene():
        print(f"[HiMYM] WARNING: {warning}")
    for attachment in attachment_objects():
        print(
            f"[HiMYM] Attachment '{attachment.get('himym_attachment_name', attachment.name)}' "
            f"axis={attachment.get('himym_direction_axis', '+Z')} node={attachment.name}"
        )

    filepath = os.path.abspath(filepath)
    os.makedirs(os.path.dirname(filepath), exist_ok=True)
    requested = {
        "filepath": filepath,
        "export_format": "GLB",
        "export_yup": True,
        "export_cameras": True,
        "export_lights": True,
        "export_extras": True,
        "export_animations": True,
        "export_apply": False,
        "use_selection": False,
    }
    supported = {
        prop.identifier
        for prop in bpy.ops.export_scene.gltf.get_rna_type().properties
    }
    options = {key: value for key, value in requested.items() if key in supported}
    result = bpy.ops.export_scene.gltf(**options)
    if "FINISHED" not in result:
        raise RuntimeError(f"glTF export failed: {result}")
    print(f"[HiMYM] Exported {filepath}")
    return filepath


def create_template(filepath):
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for collection in list(bpy.data.collections):
        bpy.data.collections.remove(collection)

    root, collection = ensure_himym_scene()

    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.0, 0.0, 0.5))
    reference = bpy.context.object
    reference.name = "Scale_Reference_1m"
    reference.parent = root
    _link_object(reference, collection)

    camera_data = bpy.data.cameras.new("Camera_Main")
    camera_data.lens = 50.0
    camera_data.clip_start = 0.1
    camera_data.clip_end = 100.0
    camera = bpy.data.objects.new("Camera_Main", camera_data)
    collection.objects.link(camera)
    camera.parent = root
    camera.location = (4.5, -6.5, 3.5)
    _look_at(camera, (0.0, 0.0, 0.75))
    bpy.context.scene.camera = camera

    light_data = bpy.data.lights.new("Key_Sun", type="SUN")
    light_data.energy = 3.0
    light = bpy.data.objects.new("Key_Sun", light_data)
    collection.objects.link(light)
    light.parent = root
    light.rotation_euler = (math.radians(25.0), math.radians(-20.0), math.radians(-30.0))

    bpy.context.scene.render.resolution_x = 1920
    bpy.context.scene.render.resolution_y = 1080
    bpy.context.scene.render.resolution_percentage = 100
    bpy.context.scene.world.color = (0.02, 0.02, 0.02)
    sync_camera_properties()

    filepath = os.path.abspath(filepath)
    os.makedirs(os.path.dirname(filepath), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=filepath)
    print(f"[HiMYM] Created template {filepath}")


class HIMYM_OT_setup_scene(bpy.types.Operator):
    bl_idname = "himym.setup_scene"
    bl_label = "Setup HiMYM Scene"
    bl_description = "Set metric units and create/reset the identity HIMYM_ROOT"

    def execute(self, context):
        ensure_himym_scene()
        self.report({"INFO"}, "HiMYM scene setup applied")
        return {"FINISHED"}


class HIMYM_OT_export_glb(bpy.types.Operator):
    bl_idname = "himym.export_glb"
    bl_label = "Export HiMYM GLB"
    bl_description = "Export using the fixed HiMYM glTF contract"

    filepath: bpy.props.StringProperty(subtype="FILE_PATH", default="//scene.glb")
    filename_ext = ".glb"

    def invoke(self, context, event):
        if bpy.data.filepath:
            base = os.path.splitext(os.path.basename(bpy.data.filepath))[0]
            self.filepath = f"//{base}.glb"
        context.window_manager.fileselect_add(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        try:
            export_glb(bpy.path.abspath(self.filepath))
        except Exception as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        self.report({"INFO"}, f"Exported {self.filepath}")
        return {"FINISHED"}


class HIMYM_OT_create_attachment(bpy.types.Operator):
    bl_idname = "himym.create_attachment"
    bl_label = "Create Attachment Socket"
    bl_description = "Create a named HiMYM attachment empty parented to the selected object or active bone"

    def execute(self, context):
        parent = context.active_object
        _, collection = ensure_himym_scene()
        socket = bpy.data.objects.new(f"{ATTACHMENT_PREFIX}Attachment", None)
        collection.objects.link(socket)
        socket.empty_display_type = "ARROWS"
        socket.empty_display_size = 0.2
        if parent is not None:
            socket.parent = parent
            if parent.type == "ARMATURE" and context.active_pose_bone is not None:
                socket.parent_type = "BONE"
                socket.parent_bone = context.active_pose_bone.name
        mark_attachment(socket)
        context.view_layer.objects.active = socket
        socket.select_set(True)
        self.report({"INFO"}, f"Created {socket.name}")
        return {"FINISHED"}


class HIMYM_OT_mark_attachment(bpy.types.Operator):
    bl_idname = "himym.mark_attachment"
    bl_label = "Mark Selected as Attachment"

    def execute(self, context):
        try:
            mark_attachment(context.active_object)
        except RuntimeError as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        self.report({"INFO"}, f"Marked {context.active_object.name} as a HiMYM attachment")
        return {"FINISHED"}


class HIMYM_OT_unmark_attachment(bpy.types.Operator):
    bl_idname = "himym.unmark_attachment"
    bl_label = "Remove Attachment Marker"

    def execute(self, context):
        obj = context.active_object
        if obj is None:
            return {"CANCELLED"}
        for key in ("himym_attachment", "himym_attachment_name", "himym_direction_axis"):
            if key in obj:
                del obj[key]
        return {"FINISHED"}


class HIMYM_PT_tools(bpy.types.Panel):
    bl_label = "HiMYM"
    bl_idname = "HIMYM_PT_tools"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "HiMYM"

    def draw(self, context):
        layout = self.layout
        layout.operator(HIMYM_OT_setup_scene.bl_idname)
        layout.separator()
        layout.operator(HIMYM_OT_create_attachment.bl_idname)
        layout.operator(HIMYM_OT_mark_attachment.bl_idname)
        obj = context.active_object
        if obj is not None and obj.get("himym_attachment", False):
            box = layout.box()
            box.label(text="Selected Attachment")
            box.prop(obj, '["himym_attachment_name"]', text="Name")
            box.prop(obj, '["himym_direction_axis"]', text="Direction Axis")
            box.operator(HIMYM_OT_unmark_attachment.bl_idname)
        layout.separator()
        layout.operator(HIMYM_OT_export_glb.bl_idname)
        warnings = validate_scene()
        if warnings:
            box = layout.box()
            box.label(text="Scene warnings:", icon="ERROR")
            for warning in warnings[:5]:
                box.label(text=warning)


CLASSES = (
    HIMYM_OT_setup_scene,
    HIMYM_OT_export_glb,
    HIMYM_OT_create_attachment,
    HIMYM_OT_mark_attachment,
    HIMYM_OT_unmark_attachment,
    HIMYM_PT_tools,
)


def register():
    for cls in CLASSES:
        try:
            bpy.utils.unregister_class(cls)
        except RuntimeError:
            pass
        bpy.utils.register_class(cls)


def unregister():
    for cls in reversed(CLASSES):
        try:
            bpy.utils.unregister_class(cls)
        except RuntimeError:
            pass


def _run_command_line():
    if "--" not in sys.argv:
        register()
        return
    parser = argparse.ArgumentParser()
    parser.add_argument("--template")
    parser.add_argument("--export")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1 :])
    if args.template:
        create_template(args.template)
    elif args.export:
        export_glb(args.export)
    else:
        register()


if __name__ == "__main__":
    _run_command_line()
