# =============================================================================
#  MODEL 600 GUI v2 ("hardware") - Blender render of the design samples.
#
#  Builds the whole UNIT 01 faceplate in 3D from tex/layout.json (the same
#  layout that produced the printed legends), then renders:
#    knob   -> renders/knob_study.png   (4 knob candidates, 3/4 view)
#    front  -> renders/unit01_front.png (orthographic: the plugin window itself)
#    hero   -> renders/unit01_hero.png  (perspective product shot for SNS)
#
#    blender -b --factory-startup -P docs/design/v2/render_hardware.py -- front [--preview]
# =============================================================================
import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
TEX = os.path.join(HERE, "tex")
OUT = os.path.join(HERE, "renders")
ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else ["front"]
SHOT = ARGS[0]
PREVIEW = "--preview" in ARGS

with open(os.path.join(TEX, "layout.json")) as f:
    LAY = json.load(f)
PW, PH = LAY["panel"]["w"], LAY["panel"]["h"]
S = 0.01                       # Blender metres per logical px.
SWEEP = 135.0


def P(x, y, z=0.0):
    """Layout px (origin top-left, y down) -> Blender (origin centre, y up)."""
    return Vector(((x - PW / 2) * S, (PH / 2 - y) * S, z))


def srgb(h, a=1.0):
    h = h.lstrip("#")
    out = []
    for i in (0, 2, 4):
        c = int(h[i:i + 2], 16) / 255.0
        out.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
    return (*out, a)


# ---- Scene -------------------------------------------------------------------------
def reset(samples):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = samples
    sc.cycles.use_adaptive_sampling = True
    sc.cycles.adaptive_threshold = 0.02
    sc.cycles.use_denoising = False
    sc.cycles.max_bounces = 8
    sc.cycles.glossy_bounces = 4
    sc.cycles.transmission_bounces = 6
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    sc.view_settings.view_transform = "AgX"
    try:
        sc.view_settings.look = "AgX - Medium High Contrast"
    except TypeError:
        pass
    return sc


def world(sc, top="#3A3F48", bottom="#08090B", strength=1.0):
    w = bpy.data.worlds.new("W")
    w.use_nodes = True
    nt = w.node_tree
    bg = nt.nodes["Background"]
    tc = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.35
    ramp.color_ramp.elements[0].color = srgb(bottom)
    ramp.color_ramp.elements[1].position = 1.0
    ramp.color_ramp.elements[1].color = srgb(top)
    nt.links.new(tc.outputs["Generated"], sep.inputs[0])
    nt.links.new(sep.outputs["Z"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], bg.inputs["Color"])
    bg.inputs["Strength"].default_value = strength
    sc.world = w


def area_light(name, loc, target, energy, size, color="#FFFFFF", size_y=None):
    l = bpy.data.lights.new(name, "AREA")
    l.energy = energy
    l.size = size
    if size_y is not None:
        l.shape = "RECTANGLE"
        l.size_y = size_y
    l.color = srgb(color)[:3]
    o = bpy.data.objects.new(name, l)
    o.location = loc
    bpy.context.scene.collection.objects.link(o)
    d = Vector(target) - Vector(loc)
    o.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
    return o


def studio_lights(key=5200.0, fill=900.0, rim=2600.0):
    # Long strip softbox high on the upper-left, grazing the panel: dark plate,
    # bright edges on every knob. Warm key, cool fill, white rim from behind.
    area_light("Key", (-9.0, 5.5, 6.5), (0, 0, 0), key, 1.6, "#FFF1E0", size_y=14.0)
    area_light("Fill", (9.0, -6.0, 6.0), (0, 0, 0), fill, 8.0, "#D9E4FF")
    area_light("Rim", (0.0, 10.0, 4.5), (0, 0, 0), rim, 14.0, "#FFFFFF", size_y=1.2)


# ---- Materials ---------------------------------------------------------------------
def mat_principled(name, color, metallic=0.0, rough=0.4, coat=0.0, aniso=0.0, emission=None, estr=0.0,
                   transmission=0.0, ior=1.45, spec=0.5, alpha=1.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = srgb(color)
    p.inputs["Metallic"].default_value = metallic
    p.inputs["Roughness"].default_value = rough
    p.inputs["IOR"].default_value = ior
    if "Specular IOR Level" in p.inputs:
        p.inputs["Specular IOR Level"].default_value = spec
    if "Coat Weight" in p.inputs:
        p.inputs["Coat Weight"].default_value = coat
    if "Anisotropic" in p.inputs:
        p.inputs["Anisotropic"].default_value = aniso
    if transmission and "Transmission Weight" in p.inputs:
        p.inputs["Transmission Weight"].default_value = transmission
    if emission:
        key = "Emission Color" if "Emission Color" in p.inputs else "Emission"
        p.inputs[key].default_value = srgb(emission)
        p.inputs["Emission Strength"].default_value = estr
    p.inputs["Alpha"].default_value = alpha
    return m


def mat_brushed_panel():
    m = mat_principled("Panel", "#2B2E33", metallic=0.85, rough=0.36, aniso=0.75)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    tc = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    # Horizontal hairline: features ~1 m long in X, ~5 mm apart in Y (about one
    # output pixel at 2x), so the grain survives the render instead of averaging out.
    mp.inputs["Scale"].default_value = (0.15, 30.0, 1.0)
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 6.0
    noise.inputs["Detail"].default_value = 8.0
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.05
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
    nt.links.new(mp.outputs["Vector"], noise.inputs["Vector"])
    nt.links.new(noise.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    # Subtle roughness variation from the same grain.
    ramp = nt.nodes.new("ShaderNodeMapRange")
    ramp.inputs["To Min"].default_value = 0.30
    ramp.inputs["To Max"].default_value = 0.42
    nt.links.new(noise.outputs["Fac"], ramp.inputs["Value"])
    nt.links.new(ramp.outputs["Result"], p.inputs["Roughness"])
    # Anisotropy tangent along world X (the plate is never rotated).
    tan = nt.nodes.new("ShaderNodeCombineXYZ")
    tan.inputs["X"].default_value = 1.0
    nt.links.new(tan.outputs["Vector"], p.inputs["Tangent"])
    return m


def mat_radial_aluminium():
    m = mat_principled("Alu", "#A9AEB4", metallic=1.0, rough=0.24, aniso=0.85)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    tc = nt.nodes.new("ShaderNodeTexCoord")
    wave = nt.nodes.new("ShaderNodeTexWave")
    wave.wave_type = "RINGS"
    wave.rings_direction = "Z"
    wave.inputs["Scale"].default_value = 140.0
    wave.inputs["Distortion"].default_value = 0.6
    wave.inputs["Detail"].default_value = 2.0
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.12
    nt.links.new(tc.outputs["Object"], wave.inputs["Vector"])
    nt.links.new(wave.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    tan = nt.nodes.new("ShaderNodeTangent")
    tan.direction_type = "RADIAL"
    tan.axis = "Z"                      # Concentric machining rings.
    nt.links.new(tan.outputs["Tangent"], p.inputs["Tangent"])
    return m


def mat_image(name, path, emission_strength=0.0, rough=0.5, decal=False):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    img = bpy.data.images.load(path)
    tx = nt.nodes.new("ShaderNodeTexImage")
    tx.image = img
    tx.interpolation = "Cubic"
    nt.links.new(tx.outputs["Color"], p.inputs["Base Color"])
    p.inputs["Roughness"].default_value = rough
    if emission_strength > 0:
        key = "Emission Color" if "Emission Color" in p.inputs else "Emission"
        nt.links.new(tx.outputs["Color"], p.inputs[key])
        p.inputs["Emission Strength"].default_value = emission_strength
    if decal:
        out = nt.nodes["Material Output"]
        mix = nt.nodes.new("ShaderNodeMixShader")
        tr = nt.nodes.new("ShaderNodeBsdfTransparent")
        nt.links.new(tx.outputs["Alpha"], mix.inputs["Fac"])
        nt.links.new(tr.outputs["BSDF"], mix.inputs[1])
        nt.links.new(p.outputs["BSDF"], mix.inputs[2])
        nt.links.new(mix.outputs["Shader"], out.inputs["Surface"])
        m.blend_method = "HASHED"
    return m


# ---- Geometry helpers --------------------------------------------------------------
def link(o):
    if o.name not in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.link(o)
    return o


def box(size, loc, mat=None, bevel=0.0, segs=3, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.scale = size
    bpy.ops.object.transform_apply(scale=True)
    if bevel:
        b = o.modifiers.new("B", "BEVEL"); b.width = bevel; b.segments = segs; b.limit_method = "NONE"
    bpy.ops.object.shade_smooth()
    if mat: o.data.materials.append(mat)
    return o


def cyl(r, depth, loc, mat=None, verts=96, bevel=0.0, r2=None, rot=(0, 0, 0)):
    if r2 is None:
        bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=depth, location=loc, rotation=rot)
    else:
        bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r, radius2=r2, depth=depth, location=loc, rotation=rot)
    o = bpy.context.active_object
    if bevel:
        b = o.modifiers.new("B", "BEVEL"); b.width = bevel; b.segments = 3; b.limit_method = "ANGLE"
    bpy.ops.object.shade_smooth()
    if mat: o.data.materials.append(mat)
    return o


def prism(points, z0, z1, mat=None, bevel=0.0, taper=1.0, centre=(0.0, 0.0)):
    """Extrude a 2D outline (Blender XY) between z0 and z1; `taper` scales the top outline."""
    me = bpy.data.meshes.new("prism")
    bm = bmesh.new()
    cx0, cy0 = centre
    bottom = [bm.verts.new((x, y, z0)) for x, y in points]
    top = [bm.verts.new((cx0 + (x - cx0) * taper, cy0 + (y - cy0) * taper, z1)) for x, y in points]
    bm.faces.new(bottom[::-1])
    bm.faces.new(top)
    n = len(points)
    for i in range(n):
        bm.faces.new((bottom[i], bottom[(i + 1) % n], top[(i + 1) % n], top[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new("prism", me)
    link(o)
    bpy.context.view_layer.objects.active = o
    o.select_set(True)
    if bevel:
        b = o.modifiers.new("B", "BEVEL"); b.width = bevel; b.segments = 3; b.limit_method = "ANGLE"
        b.angle_limit = math.radians(50)
        b.harden_normals = False
    for poly in me.polygons:
        poly.use_smooth = True
    if mat: o.data.materials.append(mat)
    return o


def boolean_cut(target, cutter):
    m = target.modifiers.new("cut", "BOOLEAN")
    m.operation = "DIFFERENCE"
    m.object = cutter
    cutter.hide_render = True
    cutter.hide_viewport = True


# ---- Parts --------------------------------------------------------------------------
M = {}


def materials():
    M["panel"] = mat_brushed_panel()
    M["black"] = mat_principled("BlackKnurl", "#0E0F11", rough=0.32, coat=0.4)
    M["bakelite"] = mat_principled("Bakelite", "#0A0A0B", rough=0.18, coat=0.8)
    M["alu"] = mat_radial_aluminium()
    M["chrome"] = mat_principled("Chrome", "#E6E8EA", metallic=1.0, rough=0.06)
    M["steel"] = mat_principled("Steel", "#9EA3A9", metallic=1.0, rough=0.28)
    M["white"] = mat_principled("Paint", "#F2EEE4", rough=0.45)
    M["bezel"] = mat_principled("Bezel", "#101113", metallic=0.2, rough=0.45, coat=0.3)
    M["glass"] = mat_principled("Glass", "#FFFFFF", rough=0.03, transmission=1.0, ior=1.45, spec=0.5)
    M["needle"] = mat_principled("Needle", "#101010", rough=0.5)
    # Illuminated caps: frosted lenses. Off = dim tinted glass, on = saturated glow.
    M["amber_off"] = mat_principled("AmberOff", "#3A2C1E", rough=0.5, coat=0.5, emission="#FFB347", estr=0.025)
    M["amber_on"] = mat_principled("AmberOn", "#FF9A2E", rough=0.35, coat=0.7, emission="#FF9426", estr=4.0)
    M["red_off"] = mat_principled("RedOff", "#3C1512", rough=0.5, coat=0.5, emission="#FF3B2A", estr=0.025)
    M["red_on"] = mat_principled("RedOn", "#E5402F", rough=0.3, coat=0.7, emission="#FF2A1A", estr=2.6)
    M["jewel"] = mat_principled("Jewel", "#C21A12", rough=0.08, coat=1.0, emission="#FF2A12", estr=1.6,
                                transmission=0.4, ior=1.6)
    M["green_on"] = mat_principled("GreenOn", "#3FD07A", rough=0.2, emission="#47E28A", estr=2.5)
    M["recess"] = mat_principled("Recess", "#050506", rough=0.8)


def screw(x, y, z):
    head = cyl(0.075, 0.03, P(x, y, z + 0.015), M["steel"], 48, bevel=0.01)
    for rot in (math.pi / 4, -math.pi / 4):
        c = box((0.11, 0.018, 0.05), P(x, y, z + 0.035), rot=(0, 0, rot))
        boolean_cut(head, c)
    return head


def knob_machined(cx, cy, r_px, angle, z0):
    """Black knurled skirt + concentric machined aluminium cap + white index."""
    r = r_px * S
    cyl(r, 0.26, P(cx, cy, z0 + 0.13), M["black"], 128, bevel=0.02)
    ridges = 120
    for i in range(ridges):
        a = 2 * math.pi * i / ridges
        o = box((0.012, 0.022, 0.2), P(cx, cy, z0 + 0.12) + Vector((math.cos(a) * r, math.sin(a) * r, 0)),
                M["black"], bevel=0.003, segs=2, rot=(0, 0, a))
    cyl(r * 0.80, 0.08, P(cx, cy, z0 + 0.30), M["black"], 128, bevel=0.02, r2=r * 0.74)
    cyl(r * 0.70, 0.04, P(cx, cy, z0 + 0.345), M["alu"], 128, bevel=0.012)
    # Index line on the skirt top bevel, pointing at `angle`.
    t = math.radians(-angle)
    d = Vector((math.sin(-t), math.cos(t), 0)) if False else Vector((math.sin(math.radians(angle)), math.cos(math.radians(angle)), 0))
    mid = P(cx, cy, z0 + 0.338) + d * (r * 0.76)
    box((0.016, r * 0.20, 0.012), mid, M["white"], rot=(0, 0, -math.radians(angle)))


def knob_chicken(cx, cy, r_px, angle, z0):
    """Bakelite chicken-head selector with a white pointer line."""
    r = r_px * S
    cyl(r * 1.05, 0.10, P(cx, cy, z0 + 0.05), M["bakelite"], 96, bevel=0.02)
    # Teardrop outline (local, pointing +Y), rotated to `angle`.
    pts = []
    head_r = r * 0.62
    tip = r * 1.55
    for i in range(48):
        a = math.pi * 0.25 + (2 * math.pi - math.pi * 0.5) * i / 47
        pts.append((math.sin(a) * head_r, math.cos(a) * head_r * 1.15 - r * 0.1))
    pts.append((0.0, tip))
    rot = -math.radians(angle)
    c = P(cx, cy)
    world_pts = [(c.x + x * math.cos(rot) - y * math.sin(rot), c.y + x * math.sin(rot) + y * math.cos(rot)) for x, y in pts]
    prism(world_pts, z0 + 0.10, z0 + 0.30, M["bakelite"], bevel=0.012, taper=0.86, centre=(c.x, c.y))
    d = Vector((math.sin(math.radians(angle)), math.cos(math.radians(angle)), 0))
    mid = P(cx, cy, z0 + 0.302) + d * (r * 0.70)
    box((0.014, r * 0.70, 0.006), mid, M["white"], rot=(0, 0, -math.radians(angle)))


def knob_fluted(cx, cy, r_px, angle, z0):
    """Vintage fluted black knob with a polished skirt (study variant)."""
    r = r_px * S
    cyl(r * 1.15, 0.05, P(cx, cy, z0 + 0.025), M["chrome"], 128, bevel=0.01)
    body = cyl(r * 0.85, 0.28, P(cx, cy, z0 + 0.19), M["bakelite"], 128, bevel=0.03, r2=r * 0.78)
    for i in range(18):
        a = 2 * math.pi * i / 18
        c = cyl(r * 0.12, 0.4, P(cx, cy, z0 + 0.2) + Vector((math.cos(a) * r * 0.9, math.sin(a) * r * 0.9, 0)), verts=24)
        boolean_cut(body, c)
    d = Vector((math.sin(math.radians(angle)), math.cos(math.radians(angle)), 0))
    mid = P(cx, cy, z0 + 0.335) + d * (r * 0.45)
    box((0.016, r * 0.7, 0.012), mid, M["white"], rot=(0, 0, -math.radians(angle)))


def knob_trim(cx, cy, r_px, angle, z0):
    """Small aluminium trim knob with a red index (study variant)."""
    r = r_px * S
    cyl(r, 0.18, P(cx, cy, z0 + 0.09), M["alu"], 96, bevel=0.02)
    d = Vector((math.sin(math.radians(angle)), math.cos(math.radians(angle)), 0))
    mid = P(cx, cy, z0 + 0.185) + d * (r * 0.55)
    red = mat_principled("TrimRed", "#D8392B", rough=0.4)
    box((0.014, r * 0.7, 0.01), mid, red, rot=(0, 0, -math.radians(angle)))


def toggle(cx, cy, on, horizontal, z0, k=1.35):
    cyl(0.085 * k, 0.03, P(cx, cy, z0 + 0.015), M["steel"], 64, bevel=0.006)
    cyl(0.06 * k, 0.05, P(cx, cy, z0 + 0.055), M["chrome"], 6, bevel=0.006)
    cyl(0.045 * k, 0.03, P(cx, cy, z0 + 0.09), M["chrome"], 48)
    tilt = math.radians(28) * (1 if on else -1)
    half = 0.13 * k
    # Lever axis: from the bushing top, leaning toward the "on" side.
    axis = Vector((math.sin(tilt), 0, math.cos(tilt))) if horizontal else Vector((0, math.sin(tilt), math.cos(tilt)))
    base = P(cx, cy, z0 + 0.1)
    rot = (0, tilt, 0) if horizontal else (-tilt, 0, 0)
    lever = cyl(0.022 * k, 2 * half, base + axis * half, M["chrome"], 32, r2=0.03 * k, rot=rot)
    tip = base + axis * (2 * half)
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.034 * k, location=tip)
    bpy.ops.object.shade_smooth()
    bpy.context.active_object.data.materials.append(M["chrome"])


def push_button(cx, cy, w, h, color, lit, z0):
    box((w * S + 0.06, h * S + 0.06, 0.05), P(cx, cy, z0 + 0.025), M["bezel"], bevel=0.015)
    box((w * S + 0.01, h * S + 0.01, 0.03), P(cx, cy, z0 + 0.06), M["recess"])
    mat = {("amber", True): M["amber_on"], ("amber", False): M["amber_off"],
           ("red", True): M["red_on"], ("red", False): M["red_off"]}[(color, lit)]
    box((w * S - 0.02, h * S - 0.02, 0.08), P(cx, cy, z0 + 0.1), mat, bevel=0.025)


def jewel(cx, cy, z0):
    cyl(0.16, 0.05, P(cx, cy, z0 + 0.025), M["chrome"], 64, bevel=0.01)
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=0.12, location=P(cx, cy, z0 + 0.06))
    o = bpy.context.active_object
    o.scale.z = 0.7
    o.data.materials.append(M["jewel"])


def led(cx, cy, z0, on=True):
    cyl(0.07, 0.03, P(cx, cy, z0 + 0.015), M["chrome"], 48, bevel=0.006)
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.05, location=P(cx, cy, z0 + 0.03))
    o = bpy.context.active_object
    o.scale.z = 0.7
    bpy.ops.object.shade_smooth()
    o.data.materials.append(M["green_on"] if on else M["recess"])


def vu_meter(z0):
    v = LAY["vu"]
    cx, cy, w, h, fw, fh = v["cx"], v["cy"], v["w"], v["h"], v["face_w"], v["face_h"]
    # Bezel frame.
    frame = box((w * S, h * S, 0.16), P(cx, cy, z0 + 0.08), M["bezel"], bevel=0.04)
    hole = box((fw * S, fh * S, 0.5), P(cx, cy, z0 + 0.1))
    boolean_cut(frame, hole)
    # Back-lit dial face.
    bpy.ops.mesh.primitive_plane_add(size=1, location=P(cx, cy, z0 + 0.02))
    face = bpy.context.active_object
    face.scale = (fw * S, fh * S, 1)
    face.data.materials.append(mat_image("VUFace", os.path.join(TEX, "vu_face.png"), emission_strength=0.9, rough=0.6))
    # Needle (from the face's bottom edge to the scale), angle by the VU law.
    vu = v["needle_vu"]
    ang = -46 + 92 * (10 ** (vu / 20) / 10 ** (3 / 20))
    pivot = P(cx, cy + fh / 2 + 40, z0 + 0.06)
    d = Vector((math.sin(math.radians(ang)), math.cos(math.radians(ang)), 0))
    r0 = (40 + 2) / math.cos(math.radians(ang)) * S
    r1 = (fh + 4) * S
    mid = pivot + d * ((r0 + r1) / 2)
    box((0.012, r1 - r0, 0.006), mid, M["needle"], rot=(0, 0, -math.radians(ang)))
    # Glass.
    bpy.ops.mesh.primitive_plane_add(size=1, location=P(cx, cy, z0 + 0.12))
    g = bpy.context.active_object
    g.scale = (fw * S, fh * S, 1)
    g.data.materials.append(M["glass"])
    # Zero-adjust screw under the window.
    cyl(0.05, 0.02, P(cx, cy + h / 2 - 9, z0 + 0.17), M["steel"], 32, bevel=0.005)


def faceplate():
    ear = LAY["panel"]["ear"]
    plate = box((PW * S, PH * S, 0.12), P(PW / 2, PH / 2, -0.06), M["panel"], bevel=0.02)
    for x, y in LAY["slots"]:
        cut = cyl(0.11, 0.6, P(x, y, 0), verts=48)
        cut.scale = (1.0, 1.6, 1)
        boolean_cut(plate, cut)
    # Ear seams (slightly darker grooves separating the ears).
    for x in (ear, PW - ear):
        box((0.02, PH * S, 0.01), P(x, PH / 2, 0.002), M["recess"])
    # Printed legends.
    bpy.ops.mesh.primitive_plane_add(size=1, location=P(PW / 2, PH / 2, 0.003))
    d = bpy.context.active_object
    d.scale = (PW * S, PH * S, 1)
    d.data.materials.append(mat_image("Decal", os.path.join(TEX, "panel_decal.png"), rough=0.55, decal=True))
    for x, y in LAY["screws"]:
        screw(x, y, 0.0)


def build_unit01():
    faceplate()
    vu_meter(0.0)
    for k in LAY["knobs_big"]:
        a = -SWEEP + (k["value"] - k["min"]) / (k["max"] - k["min"]) * 2 * SWEEP
        knob_machined(k["cx"], k["cy"], k["r"], a, 0.0)
    for k in LAY["knobs_step"]:
        n = len(k["positions"])
        a = -k["spread"] + 2 * k["spread"] * k["value"] / (n - 1)
        knob_chicken(k["cx"], k["cy"], k["r"], a, 0.0)
    for t in LAY["toggles"]:
        toggle(t["cx"], t["cy"], t["on"], t.get("horizontal", False), 0.0)
    for b in LAY["buttons"]:
        push_button(b["cx"], b["cy"], b["w"], b["h"], b["color"], b["lit"], 0.0)
    j = LAY["jewel"]
    jewel(j["cx"], j["cy"], 0.0)
    L = LAY["links"]
    for i, on in enumerate(L["lit"]):
        led(L["x"] + i * L["dx"], L["y"], 0.0, on)


# ---- Cameras -------------------------------------------------------------------------
def cam_front(sc, scale):
    cd = bpy.data.cameras.new("C")
    cd.type = "ORTHO"
    cd.ortho_scale = PW * S
    c = bpy.data.objects.new("C", cd)
    c.location = (0, 0, 20)
    link(c)
    sc.camera = c
    sc.render.resolution_x = int(PW * scale)
    sc.render.resolution_y = int(PH * scale)
    sc.render.film_transparent = True


def cam_persp(sc, loc, target, lens, w, h, focus=None, fstop=2.8):
    cd = bpy.data.cameras.new("C")
    cd.lens = lens
    if focus is not None:
        cd.dof.use_dof = True
        cd.dof.focus_distance = focus
        cd.dof.aperture_fstop = fstop
    c = bpy.data.objects.new("C", cd)
    c.location = loc
    link(c)
    d = Vector(target) - Vector(loc)
    c.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
    sc.camera = c
    sc.render.resolution_x = w
    sc.render.resolution_y = h
    sc.render.film_transparent = False


def auto_smooth(angle_deg=35.0):
    for o in bpy.context.scene.objects:
        if o.type == "MESH" and hasattr(o.data, "use_auto_smooth"):
            o.data.use_auto_smooth = True
            o.data.auto_smooth_angle = math.radians(angle_deg)


def render(sc, name):
    auto_smooth()
    os.makedirs(OUT, exist_ok=True)
    sc.render.filepath = os.path.join(OUT, name)
    bpy.ops.render.render(write_still=True)
    print("[v2] wrote", sc.render.filepath)


def shot_front():
    sc = reset(64 if PREVIEW else 384)
    world(sc, strength=0.8)
    studio_lights()
    materials()
    build_unit01()
    cam_front(sc, 1 if PREVIEW else 2)
    render(sc, "unit01_front.png")


def shot_hero():
    sc = reset(64 if PREVIEW else 384)
    world(sc, top="#2A2E36", bottom="#050506", strength=0.55)
    studio_lights()
    materials()
    build_unit01()
    # Dark stage behind the unit so the warm VU glow carries the frame.
    bpy.ops.mesh.primitive_plane_add(size=60, location=(0, 0, -0.4))
    bpy.context.active_object.data.materials.append(mat_principled("Stage", "#0B0C0E", rough=0.55))
    target = P(PW / 2 - 20, PH / 2 + 10, 0.0)
    loc = Vector((-5.8, -12.4, 9.2))
    focus_pt = P(LAY["vu"]["cx"], LAY["vu"]["cy"] + 60, 0.1)
    cam_persp(sc, loc, target, 50, 1280 if PREVIEW else 1920, 720 if PREVIEW else 1080,
              focus=(loc - focus_pt).length, fstop=4.5)
    render(sc, "unit01_hero.png")


def shot_knobs():
    global PW, PH
    sc = reset(64 if PREVIEW else 320)
    world(sc, strength=0.7)
    studio_lights()
    materials()
    PW, PH = 1100, 380
    plate = box((PW * S, PH * S, 0.12), P(PW / 2, PH / 2, -0.06), M["panel"], bevel=0.02)
    _ = plate
    knob_machined(170, 190, 60, 40, 0.0)
    knob_chicken(430, 190, 46, -30, 0.0)
    knob_fluted(690, 190, 48, 20, 0.0)
    knob_trim(930, 190, 34, -60, 0.0)
    target = P(PW / 2, PH / 2 + 10, 0.0)
    loc = (0.0, -9.6, 10.4)
    cam_persp(sc, loc, target, 50, 1280 if PREVIEW else 2200, 440 if PREVIEW else 760,
              focus=(Vector(loc) - target).length, fstop=4.0)
    render(sc, "knob_study.png")


if __name__ == "__main__":
    {"front": shot_front, "hero": shot_hero, "knob": shot_knobs}[SHOT]()
