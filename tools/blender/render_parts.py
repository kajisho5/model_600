# =============================================================================
#  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
#  tools/blender/render_parts.py  -  Renders the GUI's physical parts.
#
#  Builds each part procedurally (no external .blend files), renders it with
#  Cycles on a transparent background at 4x the logical UI size, and writes
#  PNGs to resources/parts/. The PNGs are committed, so building the plugin
#  never needs Blender; rerun this only when a part's look changes.
#
#    blender -b --factory-startup -P tools/blender/render_parts.py
#
#  Parts (logical size -> file):
#    fader cap, vertical     32 x 15  -> fader_cap_v.png
#    fader cap, horizontal   15 x 19  -> fader_cap_h.png
#    gain knob body          52 x 52  -> knob.png       (pointer drawn by Qt)
#    rack screw              12 x 12  -> screw.png
#    LED lens                10 x 10  -> led_off/green/amber/red.png
#    key backgrounds         64 x 32  -> key_default/active/danger/primary.png
#                                        (9-slice, 8 px corners)
# =============================================================================
import math
import os
import sys

import bpy

SCALE = 4  # Render pixels per logical UI pixel.
# Lighting calibration (tuned so a key face renders at its token colour).
AMBIENT = float(os.environ.get("M600_AMBIENT", "0.85"))
KEY_GAIN = float(os.environ.get("M600_KEY_GAIN", "23.0"))
KEY_SPEC = float(os.environ.get("M600_KEY_SPEC", "0.0"))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "resources", "parts")


def srgb(hex_):
    """Hex sRGB -> linear RGBA for Blender colour inputs."""
    hex_ = hex_.lstrip("#")
    out = []
    for i in (0, 2, 4):
        c = int(hex_[i:i + 2], 16) / 255.0
        out.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
    return (*out, 1.0)


# ---- Scene ---------------------------------------------------------------------
def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    # The distro build ships without OpenImageDenoise, so render clean with
    # more samples instead (the images are tiny).
    scene.cycles.samples = 512
    scene.cycles.use_denoising = False
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "8"
    scene.view_settings.view_transform = "Standard"   # UI colours must match the tokens.
    scene.view_settings.look = "None"
    scene.render.resolution_percentage = 100
    world = bpy.data.worlds.new("World")
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (1.0, 1.0, 1.0, 1.0)
    bg.inputs[1].default_value = AMBIENT
    scene.world = world
    return scene


def camera(scene, width, height, z=100.0):
    cam_data = bpy.data.cameras.new("Cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = max(width, height)
    cam = bpy.data.objects.new("Cam", cam_data)
    cam.location = (0, 0, z)
    scene.collection.objects.link(cam)
    scene.camera = cam
    scene.render.resolution_x = int(round(width * SCALE))
    scene.render.resolution_y = int(round(height * SCALE))
    return cam


def lights(scene, key=900.0, fill=180.0):
    # Key light from the top-left (UI convention), soft fill from the right.
    k = bpy.data.lights.new("Key", "AREA")
    k.energy = key * KEY_GAIN
    k.size = 60
    ko = bpy.data.objects.new("Key", k)
    ko.location = (-40, 50, 70)
    ko.rotation_euler = (math.radians(-35), math.radians(-28), 0)
    scene.collection.objects.link(ko)
    f = bpy.data.lights.new("Fill", "AREA")
    f.energy = fill * KEY_GAIN
    f.size = 80
    fo = bpy.data.objects.new("Fill", f)
    fo.location = (50, -30, 60)
    fo.rotation_euler = (math.radians(25), math.radians(35), 0)
    scene.collection.objects.link(fo)


def material(name, color, metallic=0.0, roughness=0.5, emission=None, strength=0.0, coat=0.0,
             transmission=0.0, specular=0.5):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = srgb(color)
    p.inputs["Metallic"].default_value = metallic
    p.inputs["Roughness"].default_value = roughness
    spec_key = "Specular IOR Level" if "Specular IOR Level" in p.inputs else "Specular"
    p.inputs[spec_key].default_value = specular
    if "Coat Weight" in p.inputs:
        p.inputs["Coat Weight"].default_value = coat
    if transmission and "Transmission Weight" in p.inputs:
        p.inputs["Transmission Weight"].default_value = transmission
    if emission:
        key = "Emission Color" if "Emission Color" in p.inputs else "Emission"
        p.inputs[key].default_value = srgb(emission)
        p.inputs["Emission Strength"].default_value = strength
    return m


def add_box(sx, sy, sz, loc=(0, 0, 0), bevel=0.0, segments=4, mat=None, name="Box"):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    o = bpy.context.active_object
    o.name = name
    o.scale = (sx, sy, sz)
    bpy.ops.object.transform_apply(scale=True)
    if bevel > 0:
        mod = o.modifiers.new("Bevel", "BEVEL")
        mod.width = bevel
        mod.segments = segments
        mod.limit_method = "NONE"
    bpy.ops.object.shade_smooth()
    if mat:
        o.data.materials.append(mat)
    return o


def add_cylinder(radius, depth, loc=(0, 0, 0), verts=96, bevel=0.0, mat=None, name="Cyl"):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=radius, depth=depth, location=loc)
    o = bpy.context.active_object
    o.name = name
    if bevel > 0:
        mod = o.modifiers.new("Bevel", "BEVEL")
        mod.width = bevel
        mod.segments = 4
        mod.limit_method = "ANGLE"
    bpy.ops.object.shade_smooth()
    if mat:
        o.data.materials.append(mat)
    return o


def render(scene, filename):
    path = os.path.join(OUT, filename)
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print(f"[parts] wrote {path} ({scene.render.resolution_x}x{scene.render.resolution_y})")


# ---- Parts ---------------------------------------------------------------------
def fader_cap(horizontal=False):
    scene = reset_scene()
    lights(scene)
    body = material("CapBody", "#2B3036", metallic=0.25, roughness=0.38, coat=0.3)
    stripe = material("CapLine", "#E7E9EC", roughness=0.3)
    w, h = (15.0, 19.0) if horizontal else (32.0, 15.0)
    # Body with a soft bevel and a shallow finger groove across the top.
    add_box(w - 1.0, h - 1.0, 6.0, (0, 0, 3.0), bevel=1.6, mat=body, name="Cap")
    groove = material("Groove", "#22262B", metallic=0.2, roughness=0.5)
    if horizontal:
        add_box(1.6, h - 5.0, 0.4, (0, 0, 6.05), bevel=0.15, mat=stripe, name="Line")
        for dx in (-3.6, 3.6):
            add_box(0.9, h - 6.0, 0.3, (dx, 0, 6.0), bevel=0.1, mat=groove)
    else:
        add_box(w - 9.0, 1.6, 0.4, (0, 0, 6.05), bevel=0.15, mat=stripe, name="Line")
        for dy in (-3.4, 3.4):
            add_box(w - 10.0, 0.9, 0.3, (0, dy, 6.0), bevel=0.1, mat=groove)
    camera(scene, w, h)
    render(scene, "fader_cap_h.png" if horizontal else "fader_cap_v.png")


def knob():
    scene = reset_scene()
    lights(scene, key=1100.0)
    skirt = material("Skirt", "#1C1F23", metallic=0.6, roughness=0.32)
    cap = material("CapTop", "#2E333A", metallic=0.85, roughness=0.22)
    # Skirt with knurled ridges, then a brushed metal top cap.
    add_cylinder(21.5, 8.0, (0, 0, 4.0), bevel=0.8, mat=skirt, name="Skirt")
    ridges = 96   # Fine knurl: reads as texture at UI size, not as gear teeth.
    for i in range(ridges):
        a = 2 * math.pi * i / ridges
        o = add_box(0.9, 0.7, 6.6, (math.cos(a) * 21.3, math.sin(a) * 21.3, 4.0), bevel=0.2, mat=skirt)
        o.rotation_euler = (0, 0, a)
    add_cylinder(16.5, 2.0, (0, 0, 8.6), bevel=0.9, mat=cap, name="Top")
    camera(scene, 52.0, 52.0)
    render(scene, "knob.png")


def screw():
    scene = reset_scene()
    lights(scene, key=700.0)
    metal = material("Screw", "#5A6068", metallic=1.0, roughness=0.35)
    head = add_cylinder(4.6, 1.6, (0, 0, 0.8), verts=64, bevel=0.5, mat=metal, name="Head")
    # Phillips recess: two crossed slots cut with booleans.
    for rot in (0, math.pi / 2):
        cutter = add_box(5.6, 1.0, 3.0, (0, 0, 1.8), name="Cut")
        cutter.rotation_euler = (0, 0, rot + math.pi / 4)
        mod = head.modifiers.new("Bool", "BOOLEAN")
        mod.operation = "DIFFERENCE"
        mod.object = cutter
        cutter.hide_render = True
    camera(scene, 12.0, 12.0)
    render(scene, "screw.png")


def led(name, color, strength):
    scene = reset_scene()
    lights(scene, key=500.0, fill=120.0)
    bezel = material("Bezel", "#2A2E33", metallic=0.7, roughness=0.35)
    add_cylinder(4.6, 1.0, (0, 0, 0.5), verts=64, bevel=0.3, mat=bezel, name="Bezel")
    if color:
        lens = material("Lens", color, roughness=0.15, emission=color, strength=strength)
    else:
        lens = material("Lens", "#3A4047", roughness=0.18, coat=0.6)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=24, radius=3.4, location=(0, 0, 0.9))
    dome = bpy.context.active_object
    dome.scale = (1, 1, 0.55)
    bpy.ops.object.shade_smooth()
    dome.data.materials.append(lens)
    camera(scene, 10.0, 10.0)
    render(scene, name)


def key(name, color, edge, roughness=0.55):
    scene = reset_scene()
    lights(scene, key=650.0, fill=160.0)
    face = material("KeyFace", color, metallic=0.0, roughness=roughness, specular=KEY_SPEC)
    rim = material("KeyRim", edge, metallic=0.0, roughness=0.6, specular=KEY_SPEC)
    w, h = 64.0, 32.0
    add_box(w, h, 2.0, (0, 0, 1.0), bevel=0.0, mat=rim, name="Rim")       # 1 px outline
    add_box(w - 1.2, h - 1.2, 3.2, (0, 0, 1.6), bevel=1.2, mat=face, name="Face")
    camera(scene, w, h)
    render(scene, name)


def main():
    os.makedirs(OUT, exist_ok=True)
    only = os.environ.get("M600_ONLY")
    if only == "key":
        key("key_default.png", "#23272C", "#2C3137")
        key("key_primary.png", "#F0A33A", "#F0A33A", roughness=0.4)
        return 0
    fader_cap(horizontal=False)
    fader_cap(horizontal=True)
    knob()
    screw()
    led("led_off.png", None, 0.0)
    led("led_green.png", "#46C17B", 0.7)
    led("led_amber.png", "#F0A33A", 0.7)
    led("led_red.png", "#E5534B", 0.7)
    key("key_default.png", "#23272C", "#2C3137")
    key("key_active.png", "#2E333A", "#3A4047")
    key("key_danger.png", "#2C1D1F", "#4A2A2D")
    key("key_primary.png", "#F0A33A", "#F0A33A", roughness=0.4)
    return 0


if __name__ == "__main__":
    sys.exit(main())
