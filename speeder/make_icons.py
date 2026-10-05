"""The speeder's radial portrait and map icon.

    blender -b --factory-startup --python speeder/make_icons.py
    py -3 speeder/make_icons.py <shipped portrait .dds> <shipped cd_icon_map_03.dds>

Blender renders the model twice into speeder/out/icons: portrait.png, lit
and textured at 512 px, and map.png, flat white with dark lines, both from
in front and to the left. Python then writes the two textures the plugin
serves, with the shipped files' headers:

    ui/texture/image/portraitimage/cd_mercenary_portrait_riding_speeder_1.dds
        256 px DXT5, the riding radial's picture (the ibex's is the model).
    ui/texture/cd_icon_map_speeder.dds
        128 px DXT5, the map's white silhouette with a dark rim, in a
        100 px square at the top left, as the map's style sheets tint it.
"""

import io
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ICONS = os.path.join(HERE, "out", "icons")
FILES = os.path.join(HERE, "out", "files")
PORTRAIT = "ui/texture/image/portraitimage/cd_mercenary_portrait_riding_speeder_1.dds"
MAP = "ui/texture/cd_icon_map_speeder.dds"
MAP_SIZE = 100     # the GetRect the map registry gives it
# Where the camera looks from: in front, to the left and a little above, so
# the long bike comes out about as tall as it is wide, its nose toward the
# viewer at the lower right.
VIEW = (1.0, -0.5, 0.35)


def render():
    import bpy
    from mathutils import Vector

    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    bpy.ops.wm.obj_import(filepath=os.path.join(HERE, "src", "source", "SpeederBikeForSketch.obj"))
    obj = bpy.data.objects["SpeederBikeForSketch"]
    corners = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    center = sum(corners, Vector()) / 8

    # The model's own textures on a principled surface.
    mat = obj.data.materials[0]
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    for n in [n for n in nodes if n.type not in ("OUTPUT_MATERIAL",)]:
        nodes.remove(n)
    out = next(n for n in nodes if n.type == "OUTPUT_MATERIAL")
    bsdf = nodes.new("ShaderNodeBsdfPrincipled")
    links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])

    def tex(name, color=True):
        t = nodes.new("ShaderNodeTexImage")
        t.image = bpy.data.images.load(os.path.join(HERE, "src", "textures", "SpeederBikeForSketch_" + name + ".png"))
        if not color:
            t.image.colorspace_settings.name = "Non-Color"
        return t

    ao = nodes.new("ShaderNodeMix")
    ao.data_type, ao.blend_type = "RGBA", "MULTIPLY"
    ao.inputs["Factor"].default_value = 1.0
    links.new(tex("BaseColor").outputs["Color"], ao.inputs["A"])
    links.new(tex("ao", False).outputs["Color"], ao.inputs["B"])
    links.new(ao.outputs["Result"], bsdf.inputs["Base Color"])
    links.new(tex("Metalness", False).outputs["Color"], bsdf.inputs["Metallic"])
    links.new(tex("Roughness", False).outputs["Color"], bsdf.inputs["Roughness"])
    nmap = nodes.new("ShaderNodeNormalMap")
    links.new(tex("Normal", False).outputs["Color"], nmap.inputs["Color"])
    links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])

    scene = bpy.context.scene
    scene.render.film_transparent = True
    scene.render.image_settings.color_mode = "RGBA"
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    scene.collection.objects.link(cam)
    scene.camera = cam

    def aim(direction, distance, at=center):
        v = Vector(direction).normalized()
        cam.location = at + v * distance
        cam.rotation_euler = (-v).to_track_quat("-Z", "Y").to_euler()

    def light(name, kind, energy, direction, size=200.0):
        l = bpy.data.objects.new(name, bpy.data.lights.new(name, kind))
        l.data.energy = energy
        if kind == "AREA":
            l.data.size = size
        scene.collection.objects.link(l)
        v = Vector(direction).normalized()
        l.location = center + v * 500
        l.rotation_euler = (-v).to_track_quat("-Z", "Y").to_euler()

    # Portrait: Cycles, a key light from the front left above, a fill and a
    # rim from behind, the nose toward the viewer as the ibex faces out.
    os.makedirs(ICONS, exist_ok=True)
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 96
    scene.cycles.device = "CPU"
    scene.render.resolution_x = scene.render.resolution_y = 512
    scene.world = bpy.data.worlds.new("w")
    scene.world.use_nodes = True
    bg = next(n for n in scene.world.node_tree.nodes if n.type == "BACKGROUND")
    bg.inputs["Color"].default_value = (0.35, 0.35, 0.37, 1)
    bg.inputs["Strength"].default_value = 0.6
    light("key", "AREA", 2.5e6, (0.8, -1.0, 1.2))
    light("fill", "AREA", 8e5, (0.6, 1.0, 0.3))
    light("rim", "AREA", 1.5e6, (-1.0, 0.2, 0.8))
    cam.data.type = "PERSP"
    cam.data.lens = 50
    scene.view_settings.exposure = 0.7
    aim(VIEW, 520)
    scene.render.filepath = os.path.join(ICONS, "portrait.png")
    bpy.ops.render.render(write_still=True)

    # Map: Workbench, flat white with object outlines and cavity lines, from
    # the same side.
    scene.render.engine = "BLENDER_WORKBENCH"
    shading = scene.display.shading
    shading.light = "FLAT"
    shading.color_type = "SINGLE"
    shading.single_color = (1, 1, 1)
    shading.show_object_outline = True
    shading.object_outline_color = (0, 0, 0)
    shading.show_cavity = True
    shading.cavity_type = "SCREEN"
    shading.curvature_ridge_factor = 0.0
    shading.curvature_valley_factor = 2.0
    scene.render.resolution_x = scene.render.resolution_y = 400
    scene.view_settings.exposure = 0
    aim(VIEW, 700)
    scene.render.filepath = os.path.join(ICONS, "map.png")
    bpy.ops.render.render(write_still=True)


def fit(img, size, margin):
    """The image's opaque part, scaled to fit `size` less `margin` a side and
    centred on a transparent square."""
    from PIL import Image

    box = img.getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox()
    img = img.crop(box)
    inner = size - 2 * margin
    scale = inner / max(img.size)
    img = img.resize((max(1, round(img.width * scale)), max(1, round(img.height * scale))), Image.LANCZOS)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.alpha_composite(img, ((size - img.width) // 2, (size - img.height) // 2))
    return out


def dxt5(img, template):
    """One plain DXT5 level under the shipped file's header. The game's DDS
    headers keep the top level's stored and plain sizes in the first two
    reserved words (+0x20, +0x24), and the game reads that many bytes; the
    map atlas's say 0x667E1 packed of 0x100000, which drew garbage in game
    (4 October). Here both are the level's own size, as in a shipped
    portrait, and the other reserved words stay 0."""
    b = io.BytesIO()
    img.save(b, "DDS", pixel_format="DXT5")
    blocks = b.getvalue()[128:]
    assert template[84:88] == b"DXT5" and len(blocks) == img.width * img.height
    h = bytearray(template[:128])
    struct.pack_into("<3I", h, 12, img.height, img.width, len(blocks))
    struct.pack_into("<I", h, 28, 1)
    h[0x20:0x4C] = bytes(0x4C - 0x20)
    struct.pack_into("<2I", h, 0x20, len(blocks), len(blocks))
    return bytes(h) + blocks


def write(game, data):
    path = os.path.join(FILES, *game.split("/"))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    print("wrote", game, len(data))


def convert():
    from PIL import Image, ImageFilter

    portrait_t = open(sys.argv[1], "rb").read()
    map_t = open(sys.argv[2], "rb").read()

    portrait = fit(Image.open(os.path.join(ICONS, "portrait.png")).convert("RGBA"), 256, 6)
    write(PORTRAIT, dxt5(portrait, portrait_t))

    # White where the render is, its lines kept as grey, then a grey rim
    # 4 px wide at three quarters opacity, as the ibex's has.
    src = fit(Image.open(os.path.join(ICONS, "map.png")).convert("RGBA"), MAP_SIZE, 9)
    alpha = src.getchannel("A")
    grey = src.convert("L").point(lambda v: 90 + v * 165 // 255)
    shape = Image.merge("RGBA", (grey, grey, grey, alpha))
    rim = alpha.filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.GaussianBlur(1)).point(lambda a: a * 3 // 4)
    icon = Image.merge("RGBA", (Image.new("L", rim.size, 58),) * 3 + (rim,))
    icon.alpha_composite(shape)
    sheet = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    sheet.paste(icon, (0, 0))
    sheet.save(os.path.join(ICONS, "map_icon.png"))
    portrait.save(os.path.join(ICONS, "portrait_icon.png"))
    write(MAP, dxt5(sheet, map_t))


if "bpy" in sys.modules or os.path.basename(sys.executable).lower().startswith("blender"):
    render()
else:
    convert()
