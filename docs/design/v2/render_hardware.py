# =============================================================================
#  MODEL 600 GUI v2 ("hardware") - Blender render of the design samples.
#
#  Builds each unit faceplate in 3D from tex/layout.json (the same layout that
#  produced the printed legends), then renders:
#    knob          -> renders/knob_study.png     (4 knob candidates)
#    front NN      -> renders/unitNN_front.png   (orthographic: the plugin window itself)
#    hero          -> renders/unit01_hero.png    (perspective product shot of UNIT 01)
#    rack          -> renders/rack_hero.png      (all five units stacked, 4:5 portrait)
#
#    blender -b --factory-startup -P docs/design/v2/render_hardware.py -- front 02 [--preview]
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
ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else ["front", "01"]
SHOT = ARGS[0]
PREVIEW = "--preview" in ARGS
POS = [a for a in ARGS[1:] if not a.startswith("--")]

with open(os.path.join(TEX, "layout.json")) as f:
    LAYOUT = json.load(f)
UNITS = LAYOUT["units"]
FACE_W, FACE_H = LAYOUT["face"]["w"], LAYOUT["face"]["h"]
S = 0.01                       # Blender metres per logical px.
SWEEP = 135.0

# Current unit frame: panel size and the Blender position of the panel centre.
PW, PH = 1100, 480
ORIGIN = Vector((0.0, 0.0, 0.0))


def P(x, y, z=0.0):
    """Layout px (origin top-left, y down) -> Blender (unit centre at ORIGIN, y up)."""
    return Vector(((x - PW / 2) * S + ORIGIN.x, (PH / 2 - y) * S + ORIGIN.y, z + ORIGIN.z))


def set_frame(w, h, origin=(0.0, 0.0, 0.0)):
    global PW, PH, ORIGIN
    PW, PH, ORIGIN = w, h, Vector(origin)


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
    # No caustics: chrome parts would otherwise splash noisy light onto the
    # plate (most visible above the toggle levers). Glossy filter for the same.
    sc.cycles.caustics_reflective = False
    sc.cycles.caustics_refractive = False
    sc.cycles.blur_glossy = 1.0
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


def studio_lights(key=5200.0, fill=900.0, rim=2600.0, k=1.0, centre=(0.0, 0.0, 0.0)):
    # Long strip softbox high on the upper-left, grazing the panel: dark plate,
    # bright edges on every knob. Warm key, cool fill, white rim from behind.
    # `k` scales the rig for bigger subjects (energy follows the inverse square).
    c = Vector(centre)
    e = k * k
    area_light("Key", c + Vector((-9.0, 5.5, 6.5)) * k, c, key * e, 1.6 * k, "#FFF1E0", size_y=14.0 * k)
    area_light("Fill", c + Vector((9.0, -6.0, 6.0)) * k, c, fill * e, 8.0 * k, "#D9E4FF")
    area_light("Rim", c + Vector((0.0, 10.0, 4.5)) * k, c, rim * e, 14.0 * k, "#FFFFFF", size_y=1.2 * k)


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
    # Anisotropy tangent along world X (the plates are never rotated).
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


IMAGES = {}


def mat_image(name, path, emission_strength=0.0, rough=0.5, decal=False):
    if name in IMAGES:
        return IMAGES[name]
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
    IMAGES[name] = m
    return m


def mat_screen(name, path, strength=1.2):
    """Self-lit display: pure emission from the texture (no BSDF, so no sampling noise)."""
    if name in IMAGES:
        return IMAGES[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        if n.type != "OUTPUT_MATERIAL":
            nt.nodes.remove(n)
    tx = nt.nodes.new("ShaderNodeTexImage")
    tx.image = bpy.data.images.load(path)
    tx.interpolation = "Cubic"
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Strength"].default_value = strength
    nt.links.new(tx.outputs["Color"], em.inputs["Color"])
    nt.links.new(em.outputs["Emission"], nt.nodes["Material Output"].inputs["Surface"])
    IMAGES[name] = m
    return m


def mat_thin_glass():
    """Cover glass over a display: clear transmission plus a Fresnel-weighted reflection."""
    m = bpy.data.materials.new("ThinGlass")
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        if n.type != "OUTPUT_MATERIAL":
            nt.nodes.remove(n)
    tr = nt.nodes.new("ShaderNodeBsdfTransparent")
    gl = nt.nodes.new("ShaderNodeBsdfGlossy")
    gl.inputs["Roughness"].default_value = 0.04
    fr = nt.nodes.new("ShaderNodeFresnel")
    fr.inputs["IOR"].default_value = 1.5
    mix = nt.nodes.new("ShaderNodeMixShader")
    nt.links.new(fr.outputs["Fac"], mix.inputs["Fac"])
    nt.links.new(tr.outputs["BSDF"], mix.inputs[1])
    nt.links.new(gl.outputs["BSDF"], mix.inputs[2])
    nt.links.new(mix.outputs["Shader"], nt.nodes["Material Output"].inputs["Surface"])
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


def plane(w, h, loc, mat):
    bpy.ops.mesh.primitive_plane_add(size=1, location=loc)
    o = bpy.context.active_object
    o.scale = (w, h, 1)
    o.data.materials.append(mat)
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


# ---- Materials library ------------------------------------------------------------------
M = {}
LAMP_COLORS = {"amber": ("#FF9A2E", "#FF9426", "#3A2C1E"),
               "red": ("#E5402F", "#FF2A1A", "#3C1512"),
               "green": ("#3FD07A", "#38E07E", "#16301F")}


def materials():
    M.clear()
    IMAGES.clear()
    M["panel"] = mat_brushed_panel()
    M["black"] = mat_principled("BlackKnurl", "#0E0F11", rough=0.32, coat=0.4)
    M["bakelite"] = mat_principled("Bakelite", "#0A0A0B", rough=0.18, coat=0.8)
    M["alu"] = mat_radial_aluminium()
    M["chrome"] = mat_principled("Chrome", "#E6E8EA", metallic=1.0, rough=0.06)
    M["steel"] = mat_principled("Steel", "#9EA3A9", metallic=1.0, rough=0.28)
    M["white"] = mat_principled("Paint", "#F2EEE4", rough=0.45)
    M["red_paint"] = mat_principled("TrimRed", "#D8392B", rough=0.4)
    M["bezel"] = mat_principled("Bezel", "#101113", metallic=0.2, rough=0.45, coat=0.3)
    M["glass"] = mat_principled("Glass", "#FFFFFF", rough=0.03, transmission=1.0, ior=1.45, spec=0.5)
    M["needle"] = mat_principled("Needle", "#101010", rough=0.5)
    M["thin_glass"] = mat_thin_glass()
    M["screen_off"] = mat_principled("ScreenOff", "#0B0806", rough=0.6)
    M["recess"] = mat_principled("Recess", "#050506", rough=0.8)
    M["rack"] = mat_principled("Rack", "#141518", metallic=0.6, rough=0.5)
    # Illuminated caps: frosted lenses. Off = dim tinted glass, on = saturated glow.
    M["amber_off"] = mat_principled("AmberOff", "#3A2C1E", rough=0.5, coat=0.5, emission="#FFB347", estr=0.025)
    M["amber_on"] = mat_principled("AmberOn", "#FF9A2E", rough=0.35, coat=0.7, emission="#FF9426", estr=4.0)
    M["red_off"] = mat_principled("RedOff", "#3C1512", rough=0.5, coat=0.5, emission="#FF3B2A", estr=0.025)
    M["red_on"] = mat_principled("RedOn", "#E5402F", rough=0.3, coat=0.7, emission="#FF2A1A", estr=2.6)
    M["jewel"] = mat_principled("Jewel", "#C21A12", rough=0.08, coat=1.0, emission="#FF2A12", estr=1.6,
                                transmission=0.4, ior=1.6)
    M["green_on"] = mat_principled("GreenOn", "#3FD07A", rough=0.2, emission="#47E28A", estr=2.5)
    M["led_idle"] = mat_principled("LedIdle", "#5A3A14", rough=0.2, coat=0.8, emission="#FF9A2E", estr=0.35)
    M["led_off"] = mat_principled("LedOff", "#141613", rough=0.2, coat=0.8)
    # Pilot lamps and LED ladder segments.
    for name, (base, glow, dark) in LAMP_COLORS.items():
        M[f"lamp_{name}_on"] = mat_principled(f"Lamp{name}On", base, rough=0.15, coat=1.0, emission=glow, estr=1.8)
        M[f"lamp_{name}_off"] = mat_principled(f"Lamp{name}Off", dark, rough=0.12, coat=1.0, emission=glow, estr=0.02)
        M[f"seg_{name}_on"] = mat_principled(f"Seg{name}On", base, rough=0.3, emission=glow, estr=1.6)
        M[f"seg_{name}_off"] = mat_principled(f"Seg{name}Off", dark, rough=0.35, coat=0.5)


# ---- Parts --------------------------------------------------------------------------
def screw(x, y, z):
    head = cyl(0.075, 0.03, P(x, y, z + 0.015), M["steel"], 48, bevel=0.01)
    for rot in (math.pi / 4, -math.pi / 4):
        c = box((0.11, 0.018, 0.05), P(x, y, z + 0.035), rot=(0, 0, rot))
        boolean_cut(head, c)
    return head


def index_dir(angle):
    return Vector((math.sin(math.radians(angle)), math.cos(math.radians(angle)), 0))


def knob_machined(cx, cy, r_px, angle, z0):
    """Black knurled skirt + concentric machined aluminium cap + white index."""
    r = r_px * S
    h = 0.26 * (r_px / 60) ** 0.5
    cyl(r, h, P(cx, cy, z0 + h / 2), M["black"], 128, bevel=0.02)
    ridges = int(120 * r_px / 60)
    for i in range(ridges):
        a = 2 * math.pi * i / ridges
        box((0.012, 0.022, h * 0.77), P(cx, cy, z0 + h * 0.46) + Vector((math.cos(a) * r, math.sin(a) * r, 0)),
            M["black"], bevel=0.003, segs=2, rot=(0, 0, a))
    cyl(r * 0.80, 0.08, P(cx, cy, z0 + h + 0.04), M["black"], 128, bevel=0.02, r2=r * 0.74)
    cyl(r * 0.70, 0.04, P(cx, cy, z0 + h + 0.085), M["alu"], 128, bevel=0.012)
    mid = P(cx, cy, z0 + h + 0.078) + index_dir(angle) * (r * 0.76)
    box((0.016, r * 0.20, 0.012), mid, M["white"], rot=(0, 0, -math.radians(angle)))


def knob_chicken(cx, cy, r_px, angle, z0):
    """Bakelite chicken-head selector with a white pointer line."""
    r = r_px * S
    cyl(r * 1.05, 0.10, P(cx, cy, z0 + 0.05), M["bakelite"], 96, bevel=0.02)
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
    mid = P(cx, cy, z0 + 0.302) + index_dir(angle) * (r * 0.70)
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
    mid = P(cx, cy, z0 + 0.335) + index_dir(angle) * (r * 0.45)
    box((0.016, r * 0.7, 0.012), mid, M["white"], rot=(0, 0, -math.radians(angle)))


def knob_trim(cx, cy, r_px, angle, z0):
    """Small aluminium trim knob with a red index."""
    r = r_px * S
    cyl(r, 0.18, P(cx, cy, z0 + 0.09), M["alu"], 96, bevel=0.02)
    mid = P(cx, cy, z0 + 0.185) + index_dir(angle) * (r * 0.55)
    box((0.014, r * 0.7, 0.01), mid, M["red_paint"], rot=(0, 0, -math.radians(angle)))


def toggle(cx, cy, on, horizontal, z0, k=1.35):
    toggle_base(cx, cy, z0, k)
    toggle_lever(cx, cy, on, horizontal, z0, k)


def toggle_base(cx, cy, z0, k=1.35):
    """Static part: washer, hex nut and bushing."""
    cyl(0.085 * k, 0.03, P(cx, cy, z0 + 0.015), M["steel"], 64, bevel=0.006)
    cyl(0.06 * k, 0.05, P(cx, cy, z0 + 0.055), M["chrome"], 6, bevel=0.006)
    cyl(0.045 * k, 0.03, P(cx, cy, z0 + 0.09), M["chrome"], 48)


def toggle_lever(cx, cy, on, horizontal, z0, k=1.35):
    """Moving part: bat lever and ball tip."""
    tilt = math.radians(28) * (1 if on else -1)
    half = 0.13 * k
    # Lever axis: from the bushing top, leaning toward the "on" side.
    axis = Vector((math.sin(tilt), 0, math.cos(tilt))) if horizontal else Vector((0, math.sin(tilt), math.cos(tilt)))
    base = P(cx, cy, z0 + 0.1)
    rot = (0, tilt, 0) if horizontal else (-tilt, 0, 0)
    cyl(0.022 * k, 2 * half, base + axis * half, M["chrome"], 32, r2=0.03 * k, rot=rot)
    tip = base + axis * (2 * half)
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.034 * k, location=tip)
    bpy.ops.object.shade_smooth()
    bpy.context.active_object.data.materials.append(M["chrome"])


def push_button(cx, cy, w, h, color, lit, z0):
    button_bezel(cx, cy, w, h, z0)
    button_cap(cx, cy, w, h, color, lit, z0)


def button_bezel(cx, cy, w, h, z0):
    box((w * S + 0.06, h * S + 0.06, 0.05), P(cx, cy, z0 + 0.025), M["bezel"], bevel=0.015)
    box((w * S + 0.01, h * S + 0.01, 0.03), P(cx, cy, z0 + 0.06), M["recess"])


def button_cap(cx, cy, w, h, color, lit, z0, pressed=False):
    mat = M[f"{color}_{'on' if lit else 'off'}"]
    dz = -0.02 if pressed else 0.0
    return box((w * S - 0.02, h * S - 0.02, 0.08), P(cx, cy, z0 + 0.1 + dz), mat, bevel=0.025)


def jewel(cx, cy, z0):
    cyl(0.16, 0.05, P(cx, cy, z0 + 0.025), M["chrome"], 64, bevel=0.01)
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=0.12, location=P(cx, cy, z0 + 0.06))
    o = bpy.context.active_object
    o.scale.z = 0.7
    o.data.materials.append(M["jewel"])


def led_lens(cx, cy, z0, state):
    """LINK LED lens: "on" green, "idle" dim amber (linked but silent), "off" dark."""
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.05, location=P(cx, cy, z0 + 0.03))
    o = bpy.context.active_object
    o.scale.z = 0.7
    bpy.ops.object.shade_smooth()
    o.data.materials.append({"on": M["green_on"], "idle": M["led_idle"], "off": M["led_off"]}[state])
    return o


def led_bezel(cx, cy, z0):
    cyl(0.07, 0.03, P(cx, cy, z0 + 0.015), M["chrome"], 48, bevel=0.006)


def led(cx, cy, z0, state="on"):
    led_bezel(cx, cy, z0)
    led_lens(cx, cy, z0, state)


def pilot_lamp(cx, cy, color, lit, z0):
    """Panel pilot lamp: knurled chrome bezel + smooth domed lens."""
    lamp_bezel(cx, cy, z0)
    lamp_lens(cx, cy, color, lit, z0)


def lamp_bezel(cx, cy, z0):
    cyl(0.115, 0.05, P(cx, cy, z0 + 0.025), M["chrome"], 64, bevel=0.012)
    cyl(0.085, 0.03, P(cx, cy, z0 + 0.06), M["recess"], 64)


def lamp_lens(cx, cy, color, lit, z0):
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.085, location=P(cx, cy, z0 + 0.06), segments=48, ring_count=24)
    o = bpy.context.active_object
    o.scale.z = 0.62
    bpy.ops.object.shade_smooth()
    o.data.materials.append(M[f"lamp_{color}_{'on' if lit else 'off'}"])


def ladder(L, z0):
    """Row of rectangular LEDs in a black window."""
    ladder_frame(L, z0)
    ladder_segments(L, z0)


def ladder_frame(L, z0):
    n = L["n"]
    box(((n * 16 + 12) * S, 30 * S, 0.04), P(L["cx"], L["cy"], z0 + 0.02), M["bezel"], bevel=0.01)
    box(((n * 16 + 4) * S, 22 * S, 0.02), P(L["cx"], L["cy"], z0 + 0.04), M["recess"])


def ladder_segments(L, z0):
    pitch = 16
    n = L["n"]
    x0 = L["cx"] - (n * pitch) / 2 + pitch / 2
    for i in range(n):
        frac = i / max(1, n - 1)
        color = L["colors"][0][1]
        for start, c in L["colors"]:
            if frac >= start:
                color = c
        mat = M[f"seg_{color}_{'on' if i < L['lit'] else 'off'}"]
        box((11 * S, 16 * S, 0.03), P(x0 + i * pitch, L["cy"], z0 + 0.055), mat, bevel=0.004, segs=2)


def seg_display(num, d, z0, content=True):
    """7-segment window: bezel, emissive digit plate, glass. content=False leaves a dark window."""
    cx, cy, w, h = d["cx"], d["cy"], d["w"], d["h"]
    frame = box(((w + 16) * S, (h + 16) * S, 0.12), P(cx, cy, z0 + 0.06), M["bezel"], bevel=0.03)
    hole = box((w * S, h * S, 0.5), P(cx, cy, z0 + 0.1))
    boolean_cut(frame, hole)
    if not content:
        plane(w * S, h * S, P(cx, cy, z0 + 0.01), M["screen_off"])
        return
    mat = mat_screen(f"Seg{num}{d['id']}", os.path.join(TEX, f"unit{num}_seg_{d['id']}.png"), 1.15)
    plane(w * S, h * S, P(cx, cy, z0 + 0.01), mat)
    plane(w * S, h * S, P(cx, cy, z0 + 0.09), M["thin_glass"])


def deflection(kind, value):
    if kind == "vu":
        return -46 + 92 * (10 ** (value / 20) / 10 ** (3 / 20))
    if kind == "ride":
        return 46 * value / 12
    return 46 * value


def digital_meter(num, idx, v, z0, content=True):
    """Digital meter: black bezel, recessed emissive window (7-segment value + LED bar), glass."""
    cx, cy, w, h = v["cx"], v["cy"], v["w"], v["h"]
    m = v["digi"]
    ww, wh = m["win_w"], m["win_h"]
    frame = box((w * S, h * S, 0.14), P(cx, cy, z0 + 0.07), M["bezel"], bevel=0.03)
    hole = box((ww * S, wh * S, 0.5), P(cx, cy, z0 + 0.1))
    boolean_cut(frame, hole)
    if not content:
        plane(ww * S, wh * S, P(cx, cy, z0 + 0.01), M["screen_off"])
        return
    mat = mat_screen(f"Meter{num}_{idx}", os.path.join(TEX, f"unit{num}_meter_{idx}.png"), 1.15)
    plane(ww * S, wh * S, P(cx, cy, z0 + 0.01), mat)
    plane(ww * S, wh * S, P(cx, cy, z0 + 0.11), M["thin_glass"])


def vu_meter(v, z0):
    cx, cy, w, h, fw, fh = v["cx"], v["cy"], v["w"], v["h"], v["face_w"], v["face_h"]
    k = fw / FACE_W
    frame = box((w * S, h * S, 0.16), P(cx, cy, z0 + 0.08), M["bezel"], bevel=0.04 * min(1.0, k + 0.2))
    hole = box((fw * S, fh * S, 0.5), P(cx, cy, z0 + 0.1))
    boolean_cut(frame, hole)
    face = mat_image(f"Face_{v['face']}", os.path.join(TEX, f"face_{v['face']}.png"), emission_strength=0.9, rough=0.6)
    plane(fw * S, fh * S, P(cx, cy, z0 + 0.02), face)
    # Needle: from the face's bottom edge to the scale; the pivot sits below the window.
    ang = deflection(v["face"], v["needle"])
    pivot = P(cx, cy + fh / 2 + 40 * k, z0 + 0.06)
    d = index_dir(ang)
    r0 = (40 * k + 2) / math.cos(math.radians(ang)) * S
    r1 = (FACE_H + 4) * k * S
    box((0.012, r1 - r0, 0.006), pivot + d * ((r0 + r1) / 2), M["needle"], rot=(0, 0, -math.radians(ang)))
    plane(fw * S, fh * S, P(cx, cy, z0 + 0.12), M["glass"])
    cyl(0.05 * max(0.7, k), 0.02, P(cx, cy + h / 2 - 9 * max(0.7, k), z0 + 0.17), M["steel"], 32, bevel=0.005)


def faceplate(num, u, static=False):
    w, h = u["panel"]["w"], u["panel"]["h"]
    ear = u["panel"]["ear"]
    plate = box((w * S, h * S, 0.12), P(w / 2, h / 2, -0.06), M["panel"], bevel=0.02)
    for x, y in u["slots"]:
        cut = cyl(0.11, 0.6, P(x, y, 0), verts=48)
        cut.scale = (1.0, 1.6, 1)
        boolean_cut(plate, cut)
    for x in (ear, w - ear):
        box((0.02, h * S, 0.01), P(x, h / 2, 0.002), M["recess"])
    plane(w * S, h * S, P(w / 2, h / 2, 0.003),
          mat_image(f"Decal{num}{'s' if static else ''}",
                    os.path.join(TEX, f"unit{num}_decal{'_static' if static else ''}.png"), rough=0.55, decal=True))
    for x, y in u["screws"]:
        screw(x, y, 0.0)


def build_unit(num, origin=(0.0, 0.0, 0.0)):
    u = UNITS[num]
    set_frame(u["panel"]["w"], u["panel"]["h"], origin)
    faceplate(num, u)
    for i, v in enumerate(u["vus"]):
        if "digi" in v:
            digital_meter(num, i, v, 0.0)
        else:
            vu_meter(v, 0.0)
    for k in u["knobs_big"]:
        a = -SWEEP + (k["value"] - k["min"]) / (k["max"] - k["min"]) * 2 * SWEEP
        knob_machined(k["cx"], k["cy"], k["r"], a, 0.0)
    for k in u["knobs_step"]:
        n = len(k["positions"])
        a = -k["spread"] + 2 * k["spread"] * k["value"] / (n - 1)
        knob_chicken(k["cx"], k["cy"], k["r"], a, 0.0)
    for k in u["knobs_trim"]:
        a = -SWEEP + (k["value"] - k["min"]) / (k["max"] - k["min"]) * 2 * SWEEP
        knob_trim(k["cx"], k["cy"], k["r"], a, 0.0)
    for t in u["toggles"]:
        toggle(t["cx"], t["cy"], t["on"], t.get("horizontal", False), 0.0)
    for b in u["buttons"]:
        push_button(b["cx"], b["cy"], b["w"], b["h"], b["color"], b["lit"], 0.0)
    for l in u["lamps"]:
        pilot_lamp(l["cx"], l["cy"], l["color"], l["lit"], 0.0)
    for L in u["ladders"]:
        ladder(L, 0.0)
    for d in u["displays"]:
        seg_display(num, d, 0.0)
    j = u["jewel"]
    jewel(j["cx"], j["cy"], 0.0)
    L = u["links"]
    for i, state in enumerate(L["state"]):
        led(L["x"] + i * L["dx"], L["y"], 0.0, state)


# ---- Cameras -------------------------------------------------------------------------
def cam_front(sc, w, h, scale):
    cd = bpy.data.cameras.new("C")
    cd.type = "ORTHO"
    cd.ortho_scale = max(w, h) * S
    c = bpy.data.objects.new("C", cd)
    c.location = (0, 0, 20)
    link(c)
    sc.camera = c
    sc.render.resolution_x = int(w * scale)
    sc.render.resolution_y = int(h * scale)
    sc.render.film_transparent = True


def cam_persp(sc, loc, target, lens, w, h, focus=None, fstop=2.8, up=None):
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
    if up is None:
        c.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()   # Camera up = world +Z.
    else:
        # Explicit world up vector (the rack stands in the XY plane, up = +Y).
        from mathutils import Matrix
        f = d.normalized()
        right = f.cross(Vector(up)).normalized()
        cam_up = right.cross(f)
        c.rotation_euler = Matrix((right, cam_up, -f)).transposed().to_euler()
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


# ---- Shots ---------------------------------------------------------------------------
def shot_front(num):
    sc = reset(64 if PREVIEW else 256)
    world(sc, strength=0.8)
    studio_lights()
    materials()
    build_unit(num)
    cam_front(sc, PW, PH, 1 if PREVIEW else 2)
    render(sc, f"unit{num}_front.png")


def shot_hero():
    sc = reset(64 if PREVIEW else 384)
    world(sc, top="#2A2E36", bottom="#050506", strength=0.55)
    studio_lights()
    materials()
    build_unit("01")
    # Dark stage behind the unit so the warm VU glow carries the frame.
    bpy.ops.mesh.primitive_plane_add(size=60, location=(0, 0, -0.4))
    bpy.context.active_object.data.materials.append(mat_principled("Stage", "#0B0C0E", rough=0.55))
    target = P(PW / 2 - 20, PH / 2 + 10, 0.0)
    loc = Vector((-5.8, -12.4, 9.2))
    v = UNITS["01"]["vus"][0]
    focus_pt = P(v["cx"], v["cy"] + 60, 0.1)
    cam_persp(sc, loc, target, 50, 1280 if PREVIEW else 1920, 720 if PREVIEW else 1080,
              focus=(loc - focus_pt).length, fstop=4.5)
    render(sc, "unit01_hero.png")


def shot_rack():
    """All five units stacked like a rack, shot from the front-left. 4:5 portrait for SNS."""
    sc = reset(48 if PREVIEW else 192)
    world(sc, top="#22262D", bottom="#040405", strength=0.5)
    gap = 3                                  # px between units.
    order = ["01", "02", "03", "04", "05"]
    total = sum(UNITS[n]["panel"]["h"] for n in order) + gap * (len(order) - 1)
    studio_lights(k=2.6, centre=(0.0, 0.0, 0.0))
    materials()
    top = total / 2 * S
    for n in order:
        h = UNITS[n]["panel"]["h"]
        cy = top - h / 2 * S
        build_unit(n, (0.0, cy, 0.0))
        top -= (h + gap) * S
    # Rack body behind the faceplates.
    box((11.4, total * S + 0.4, 1.2), (0, 0, -0.75), M["rack"], bevel=0.04)
    bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, -1.5))
    bpy.context.active_object.data.materials.append(mat_principled("Wall", "#0A0B0D", rough=0.7))
    loc = Vector((-17.0, -4.4, 38.5))
    target = Vector((0.6, -0.6, 0.0))
    cam_persp(sc, loc, target, 50, 640 if PREVIEW else 1280, 800 if PREVIEW else 1600,
              focus=(loc - Vector((0.0, 2.0, 0.2))).length, fstop=8.0, up=(0.0, 1.0, 0.0))
    render(sc, "rack_hero.png")


def shot_knobs():
    sc = reset(64 if PREVIEW else 320)
    world(sc, strength=0.7)
    studio_lights()
    materials()
    set_frame(1100, 380)
    box((PW * S, PH * S, 0.12), P(PW / 2, PH / 2, -0.06), M["panel"], bevel=0.02)
    knob_machined(170, 190, 60, 40, 0.0)
    knob_chicken(430, 190, 46, -30, 0.0)
    knob_fluted(690, 190, 48, 20, 0.0)
    knob_trim(930, 190, 34, -60, 0.0)
    target = P(PW / 2, PH / 2 + 10, 0.0)
    loc = (0.0, -9.6, 10.4)
    cam_persp(sc, loc, target, 50, 1280 if PREVIEW else 2200, 440 if PREVIEW else 760,
              focus=(Vector(loc) - target).length, fstop=4.0)
    render(sc, "knob_study.png")


# ---- Qt assets ------------------------------------------------------------------------
#  Static background per unit (everything that never moves), plus sprites for every
#  moving part, rendered with the same lights from the same orthographic camera so
#  they composite seamlessly. Moving parts are present in the background render as
#  camera-invisible proxies: they still cast their shadows and show in reflections.
FRAMES = os.environ.get("M600_FRAMES", os.path.join(HERE, "_frames"))
BIG_FRAMES = 61                       # 4.5 degree steps over the 270 degree sweep.


def created(fn, *a, **kw):
    before = set(bpy.data.objects)
    fn(*a, **kw)
    return [o for o in bpy.data.objects if o not in before]


def hide_from_camera(objs):
    for o in objs:
        o.visible_camera = False


def knob_angle_of(k):
    return -SWEEP + (k["value"] - k["min"]) / (k["max"] - k["min"]) * 2 * SWEEP


def selector_angles(k):
    n = len(k["positions"])
    return [round(-k["spread"] + (2 * k["spread"] * i / (n - 1) if n > 1 else 0), 2) for i in range(n)]


def build_static(num):
    u = UNITS[num]
    set_frame(u["panel"]["w"], u["panel"]["h"])
    faceplate(num, u, static=True)
    for i, v in enumerate(u["vus"]):
        digital_meter(num, i, v, 0.0, content=False)
    for d in u["displays"]:
        seg_display(num, d, 0.0, content=False)
    for L in u["ladders"]:
        ladder_frame(L, 0.0)
    for t in u["toggles"]:
        toggle_base(t["cx"], t["cy"], 0.0)
        lever = created(toggle_lever, t["cx"], t["cy"], t["on"], t.get("horizontal", False), 0.0)
        hide_from_camera(lever)
        for o in lever:   # The bright chrome ball would sparkle in the brushed plate.
            o.visible_glossy = False
    for b in u["buttons"]:
        button_bezel(b["cx"], b["cy"], b["w"], b["h"], 0.0)
        hide_from_camera(created(button_cap, b["cx"], b["cy"], b["w"], b["h"], b["color"], False, 0.0))
    for l in u["lamps"]:
        lamp_bezel(l["cx"], l["cy"], 0.0)
    for k in u["knobs_big"]:
        hide_from_camera(created(knob_machined, k["cx"], k["cy"], k["r"], knob_angle_of(k), 0.0))
    for k in u["knobs_step"]:
        hide_from_camera(created(knob_chicken, k["cx"], k["cy"], k["r"], selector_angles(k)[k["value"]], 0.0))
    for k in u["knobs_trim"]:
        hide_from_camera(created(knob_trim, k["cx"], k["cy"], k["r"], 0.0, 0.0))
    j = u["jewel"]
    jewel(j["cx"], j["cy"], 0.0)
    L = u["links"]
    for i in range(5):
        led_bezel(L["x"] + i * L["dx"], L["y"], 0.0)


def shot_backgrounds(nums):
    os.makedirs(FRAMES, exist_ok=True)
    for num in nums:
        sc = reset(48 if PREVIEW else 256)
        world(sc, strength=0.8)
        studio_lights()
        materials()
        build_static(num)
        cam_front(sc, PW, PH, 1 if PREVIEW else 2)
        auto_smooth()
        sc.render.filepath = os.path.join(FRAMES, f"bg_{num}.png")
        bpy.ops.render.render(write_still=True)
        print("[v2] wrote", sc.render.filepath)


def sprite_scene(samples):
    sc = reset(samples)
    world(sc, strength=0.8)
    studio_lights()
    materials()
    set_frame(1100, 480)
    # The plate is invisible to the camera but still reflects into chrome and casts bounce light.
    plate = box((PW * S, PH * S, 0.12), P(PW / 2, PH / 2, -0.06), M["panel"], bevel=0.02)
    plate.visible_camera = False
    sc.render.film_transparent = True
    return sc


def sprite_cam(sc, cx, cy, w, h, scale=2):
    for o in list(bpy.data.objects):
        if o.type == "CAMERA":
            bpy.data.objects.remove(o, do_unlink=True)
    cd = bpy.data.cameras.new("C")
    cd.type = "ORTHO"
    cd.ortho_scale = max(w, h) * S
    c = bpy.data.objects.new("C", cd)
    c.location = P(cx, cy, 20)
    link(c)
    sc.camera = c
    sc.render.resolution_x = int(w * scale)
    sc.render.resolution_y = int(h * scale)


def render_sprite(sc, name, objs):
    auto_smooth()
    sc.render.filepath = os.path.join(FRAMES, name + ".png")
    bpy.ops.render.render(write_still=True)
    for o in objs:
        bpy.data.objects.remove(o, do_unlink=True)


def shot_sprites():
    os.makedirs(FRAMES, exist_ok=True)
    sc = sprite_scene(32 if PREVIEW else 128)
    cx, cy = 550, 240
    manifest = {}
    # Knob filmstrips.
    angles = [-SWEEP + 2 * SWEEP * i / (BIG_FRAMES - 1) for i in range(BIG_FRAMES)]
    for name, fn, r, margin in (("knob_big", knob_machined, 54, 8), ("knob_mid", knob_machined, 42, 8),
                                ("knob_trim", knob_trim, 17, 6)):
        size = 2 * (r + margin)
        sprite_cam(sc, cx, cy, size, size)
        frames = angles if not PREVIEW else angles[:: (BIG_FRAMES - 1) // 4]
        for i, a in enumerate(frames):
            render_sprite(sc, f"{name}_{i:03d}", created(fn, cx, cy, r, a, 0.0))
        manifest[name] = {"size": size, "radius": r, "angles": frames}
    # Selector (chicken head): exactly the detents used on the panels.
    sel = sorted({a for u in UNITS.values() for k in u["knobs_step"] for a in selector_angles(k)})
    size = 2 * (round(28 * 1.55) + 8)
    sprite_cam(sc, cx, cy, size, size)
    for i, a in enumerate(sel):
        render_sprite(sc, f"selector_{i:03d}", created(knob_chicken, cx, cy, 28, a, 0.0))
    manifest["selector"] = {"size": size, "radius": 28, "angles": sel}
    # Toggle levers (the nut is in the background).
    sprite_cam(sc, cx, cy, 64, 64)
    for name, on, hz in (("toggle_up", True, False), ("toggle_down", False, False),
                         ("toggle_right", True, True), ("toggle_left", False, True)):
        render_sprite(sc, name, created(toggle_lever, cx, cy, on, hz, 0.0))
        manifest[name] = {"size": 64}
    # Push-button caps, released and pressed, lit and dark.
    for w, h in ((54, 34), (52, 32)):
        sprite_cam(sc, cx, cy, w + 10, h + 10)
        for color in ("amber", "red"):
            for lit in (False, True):
                for pressed in (False, True):
                    name = f"cap_{color}_{'on' if lit else 'off'}{'_down' if pressed else ''}_{w}x{h}"
                    render_sprite(sc, name, created(button_cap, cx, cy, w, h, color, lit, 0.0, pressed))
                    manifest[name] = {"w": w + 10, "h": h + 10}
    # Pilot lamp lenses and LINK LED lenses.
    sprite_cam(sc, cx, cy, 28, 28)
    for color in ("amber", "green", "red"):
        for lit in (False, True):
            name = f"lamp_{color}_{'on' if lit else 'off'}"
            render_sprite(sc, name, created(lamp_lens, cx, cy, color, lit, 0.0))
            manifest[name] = {"size": 28}
    sprite_cam(sc, cx, cy, 16, 16)
    for state in ("on", "idle", "off"):
        render_sprite(sc, f"led_{state}", created(led_lens, cx, cy, 0.0, state))
        manifest[f"led_{state}"] = {"size": 16}
    with open(os.path.join(FRAMES, "sprites.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    print("[v2] sprites done:", len(manifest))


if __name__ == "__main__":
    if SHOT == "front":
        for n in (POS or ["01"]):
            shot_front(n)
    elif SHOT == "hero":
        shot_hero()
    elif SHOT == "rack":
        shot_rack()
    elif SHOT == "knob":
        shot_knobs()
    elif SHOT == "backgrounds":
        shot_backgrounds(POS or list(UNITS))
    elif SHOT == "sprites":
        shot_sprites()
    else:
        raise SystemExit(f"unknown shot {SHOT}")
