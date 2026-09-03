#!/usr/bin/env python3
# enterprise-hud3d.py — Enterprise with a coplanar 3D slughorn ring.
#
# The ring lives in world space, coplanar with the Enterprise saucer section.
# As the model rotates under the TrackballManipulator, the ring rotates with it.
#
# Pipeline (5 passes):
#   [Scene RTT]  PRE 0  — Enterprise + ring (Fresnel + ring shaders) → sceneColor + sceneDepth
#   [Edge RTT]   PRE 1  — depth-Sobel edge glow                      → edgeTex
#   [Blur H]     PRE 2  — Gaussian bloom on edgeTex                  → blurA
#   [Blur V]     PRE 3  —                                             → blurB
#   [Composite]  POST   — scene + bloom + blue tint + scanlines + vignette
#
# Usage: python enterprise-hud3d.py path/to/enterprise.osgt

from OpenSceneGraph import *
from OpenSceneGraph.GL import *

import osgSlug
import slughorn
import math
import time
import sys
import os

W, H = 800, 600
HUD_EFFECTS = False

os.environ.update({
    "OSG_WINDOW":                  f"50 50 {W} {H}",
    "OSG_THREADING":               "SingleThreaded",
    "OSG_GL_CONTEXT_PROFILE_MASK": "1",
    "OSG_GL_VERSION":              "4.6",
    "OSG_GL_CONTEXT_VERSION":      "4.6",
})

# ---------------------------------------------------------------------------
# Shaders
# ---------------------------------------------------------------------------

FULLSCREEN_VERT = """
#version 330 core
in vec4 osg_Vertex;
in vec2 osg_MultiTexCoord0;
out vec2 uv;
void main() {
    uv          = osg_MultiTexCoord0;
    gl_Position = osg_Vertex;
}
"""

HOLOGRAM_VERT = """
#version 330 core
in vec4 osg_Vertex;
in vec3 osg_Normal;
uniform mat4 osg_ModelViewProjectionMatrix;
uniform mat4 osg_ModelViewMatrix;
uniform mat3 osg_NormalMatrix;
out vec3 vNormal;
out vec3 vPosition;
void main() {
    vec4 posEye = osg_ModelViewMatrix * osg_Vertex;
    vPosition   = posEye.xyz;
    vNormal     = normalize(osg_NormalMatrix * osg_Normal);
    gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;
}
"""

HOLOGRAM_FRAG = """
#version 330 core
in vec3 vNormal;
in vec3 vPosition;

uniform vec3  hudColor;
uniform float rimPower;

uniform float geometryIndex;
uniform float scanIndex;
uniform float scanWidth;
uniform float scanPulse;
uniform vec3  highlightColor;

out vec4 fragColor;

void main() {
    vec3  N   = normalize(vNormal);
    vec3  V   = normalize(-vPosition);
    float rim = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), rimPower);

    vec3 base = hudColor * (0.04 + rim);

    float d = abs(geometryIndex - scanIndex);
    float h = smoothstep(scanWidth, 0.0, d);
    h *= 0.25 + 0.75 * scanPulse;

    vec3 color = base + highlightColor * h * (0.25 + rim * 1.5);

    fragColor = vec4(color, 1.0);
}
"""

EDGE_FRAG = """
#version 330 core
uniform sampler2D inputTex;
uniform sampler2D depthTex;
uniform float     znear;
uniform float     zfar;
uniform vec2      texelSize;
uniform vec3      hudColor;
in  vec2 uv;
out vec4 fragColor;

float linearize(float d, float n, float f) {
    float z = d * 2.0 - 1.0;
    return (2.0 * n * f) / (f + n - z * (f - n));
}

void main() {
    vec4  scene = texture(inputTex, uv);
    float d     = texture(depthTex, uv).r;

    // if (d >= 1.0) { fragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }
    if (d >= 1.0) { fragColor = scene; return; }

    float dL = texture(depthTex, uv + vec2(-texelSize.x, 0.0        )).r;
    float dR = texture(depthTex, uv + vec2( texelSize.x, 0.0        )).r;
    float dU = texture(depthTex, uv + vec2( 0.0,         texelSize.y)).r;
    float dD = texture(depthTex, uv + vec2( 0.0,        -texelSize.y)).r;

    float z  = linearize(d,  znear, zfar);
    float zL = linearize(dL, znear, zfar);
    float zR = linearize(dR, znear, zfar);
    float zU = linearize(dU, znear, zfar);
    float zD = linearize(dD, znear, zfar);

    float edge    = sqrt(pow(zL - zR, 2.0) + pow(zU - zD, 2.0));
    float thresh  = z * 0.03;
    float outline = smoothstep(thresh * 0.5, thresh * 1.5, edge);

    fragColor = vec4(scene.rgb + hudColor * outline, 1.0);
}
"""

BLUR_FRAG = """
#version 330 core
uniform sampler2D inputTex;
uniform vec2      texelStep;
in  vec2 uv;
out vec4 fragColor;
void main() {
    vec2 s = texelStep * 6.0;
    vec4 c = vec4(0.0);
    c += texture(inputTex, uv - 4.0 * s) * 0.0162162162;
    c += texture(inputTex, uv - 3.0 * s) * 0.0540540541;
    c += texture(inputTex, uv - 2.0 * s) * 0.1216216216;
    c += texture(inputTex, uv - 1.0 * s) * 0.1945945946;
    c += texture(inputTex, uv           ) * 0.2270270270;
    c += texture(inputTex, uv + 1.0 * s) * 0.1945945946;
    c += texture(inputTex, uv + 2.0 * s) * 0.1216216216;
    c += texture(inputTex, uv + 3.0 * s) * 0.0540540541;
    c += texture(inputTex, uv + 4.0 * s) * 0.0162162162;
    fragColor = c;
}
"""

# Ring rotation — identical to enterprise-hud.py; operates in local canvas space.
# After the MatrixTransform (90° Rx), local-XY rotation becomes world-XZ rotation,
# keeping the ring coplanar with the saucer as the model tumbles.
VERT_EFFECTS = """
#version 430 core
#pragma osgSlug lib_vertex

vec3 osgSlug_Vertex(
    vec3 pos, vec2 emCoord, vec2 uv,
    int effectId, vec2 origin, float effectParam, float time
) {
    if (effectId == 1) {
        float angle = effectParam * time;
        float c = cos(angle), s = sin(angle);
        mat2  R = mat2(c, s, -s, c);
        vec2  pivot = pos.xy - emCoord.xy + origin;
        pos.xy = R * (pos.xy - pivot) + pivot;
    }
    return pos;
}
"""

# Composite: enterprise and HUD are separate subgraphs with independent bloom.
COMPOSITE_FRAG = """
#version 330 core
uniform sampler2D glowTex;       // enterprise scene with Sobel edges
uniform sampler2D entBlurTex;    // enterprise Gaussian bloom
uniform sampler2D hudTex;        // raw HUD color
uniform sampler2D hudBlurTex;    // HUD Gaussian bloom
uniform float     glowStrength;
uniform float     hudGlowStrength;
in  vec2 uv;
out vec4 fragColor;

void main() {
    vec3 ent      = texture(glowTex,    uv).rgb;
    vec3 entBloom = texture(entBlurTex, uv).rgb;
    vec3 hud      = texture(hudTex,     uv).rgb;
    vec3 hudBloom = texture(hudBlurTex, uv).rgb;

    // Enterprise: blue tint + bloom + scanlines + vignette.
    vec3 entColor = ent * vec3(0.25, 0.55, 1.00) + entBloom * glowStrength;

    float scan     = step(0.35, fract(gl_FragCoord.y * 0.25));
    scan           = mix(0.72, 1.0, scan);

    float d        = distance(uv, vec2(0.5));
    float vignette = smoothstep(0.72, 0.12, d);

    entColor *= scan * vignette;

    // HUD: added on top without tint/scanlines/vignette.
    vec3 hudColor = hud + hudBloom * hudGlowStrength;

    fragColor = vec4(entColor + hudColor, 1.0);
}
"""

# ---------------------------------------------------------------------------
# Shield HUD parameters
# ---------------------------------------------------------------------------

CX, CY       = 0.5, 0.5
BASE_COLOR   = slughorn.Color(0.6, 0.85, 1.0, 0.6)
TEXT_COLOR   = slughorn.Color(0.6, 0.85, 1.0, 0.9)

FONT_PATH    = "/home/cubicool/dev/osgSlug/BUILD-g++-13.3.0-NOASAN/font/Orbitron-VariableFont_wght.ttf"
FONT_SIZE    = 0.038

ARC_RADIUS   = 0.42   # sector arc outer radius
FILL_RADIUS  = 0.18   # sector fill inner radius (leaves room for the ship)
TEXT_RADIUS  = 0.485  # Orbitron label arc (outside sector arc)
TICK_INNER   = 0.39   # radial tick inner radius
TICK_OUTER   = 0.45   # radial tick outer radius
ARC_WIDTH    = 0.012  # stroke width for sector arcs
TICK_WIDTH   = 0.006  # stroke width for divider ticks
FILL_COLOR   = slughorn.Color(0.3, 0.55, 0.9, 0.18)  # dim blue sector fill

SECTOR_HALF  = math.radians(40)   # each sector spans ±40° = 80° total
GAP_HALF     = math.radians(10)   # 10° gap on each side of each divider

# Sectors: center angle (radians), label.
# 0 = +X = FORE (ship points right in the demo).
SECTORS = [
    {"name": "fore",      "label": "FORE",      "center": 0.0},
    {"name": "starboard", "label": "STARBOARD", "center": math.pi / 2},
    {"name": "aft",       "label": "AFT",        "center": math.pi},
    {"name": "port",      "label": "PORT",       "center": 3 * math.pi / 2},
]

# Tick marks sit at the midpoint of each gap (45°, 135°, 225°, 315°).
TICK_ANGLES = [math.pi / 4 + i * math.pi / 2 for i in range(4)]

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def make_color_texture(w, h):
    tex = osg.Texture2D()
    tex.size           = (w, h)
    tex.internalFormat = GL_RGBA16F
    tex.filter         = (osg.Texture.LINEAR, osg.Texture.LINEAR)
    tex.wrap           = (osg.Texture.CLAMP_TO_EDGE, osg.Texture.CLAMP_TO_EDGE)
    return tex

def make_depth_texture(w, h):
    tex = osg.Texture2D()
    tex.size           = (w, h)
    tex.internalFormat = GL_DEPTH_COMPONENT24
    tex.sourceFormat   = GL_DEPTH_COMPONENT
    tex.sourceType     = GL_FLOAT
    tex.filter         = (osg.Texture.NEAREST, osg.Texture.NEAREST)
    return tex

def make_fullscreen_quad():
    g = osg.Geode(name="fullscreen-quad")
    g.drawables.append(osg.createTexturedQuadGeometry(
        osg.Vec3(-1, -1, -1), osg.Vec3(2, 0, 0), osg.Vec3(0, 2, 0),
    ))
    return g

def make_program(name, vert, frag):
    return osg.Program(name=name, shaders=(
        osg.Shader(osg.Shader.VERTEX,   vert),
        osg.Shader(osg.Shader.FRAGMENT, frag),
    ))

def make_scene_rtt_pass(color_tex, depth_tex, scene, w, h, name="Scene RTT", order=0):
    cam = osg.Camera()
    cam.name                       = name
    cam.renderOrder                = (osg.Camera.PRE_RENDER, order)
    cam.renderTargetImplementation = osg.Camera.FRAME_BUFFER_OBJECT
    cam.clearMask                  = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT
    cam.clearColor                 = osg.Vec4(0, 0, 0, 1)
    cam.viewport                   = osg.Viewport(0, 0, w, h)
    # cam.attach(osg.Camera.COLOR_BUFFER, color_tex)
    cam.attach(osg.Camera.COLOR_BUFFER, color_tex, 0, 0, False, 8, 8)
    cam.attach(osg.Camera.DEPTH_BUFFER, depth_tex)
    cam.children.append(scene)
    return cam

def make_fullscreen_rtt_pass(
    input_tex, output_tex, frag_shader, w, h,
    name="Post RTT", order=1,
    extra_uniforms=None, extra_textures=None,
):
    cam = osg.Camera()
    cam.name                       = name
    cam.renderOrder                = (osg.Camera.PRE_RENDER, order)
    cam.dataVariance               = osg.Object.DYNAMIC
    cam.renderTargetImplementation = osg.Camera.FRAME_BUFFER_OBJECT
    cam.clearMask                  = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT
    cam.clearColor                 = osg.Vec4(0, 0, 0, 1)
    cam.viewport                   = osg.Viewport(0, 0, w, h)
    cam.projectionMatrix           = osg.Matrix.identity()
    cam.viewMatrix                 = osg.Matrix.identity()
    cam.referenceFrame             = osg.Transform.ABSOLUTE_RF
    cam.allowEventFocus            = False
    # cam.attach(osg.Camera.COLOR_BUFFER, output_tex)
    cam.attach(osg.Camera.COLOR_BUFFER, output_tex, 0, 0, False, 8, 8)

    prog = make_program(f"{name}_prog", FULLSCREEN_VERT, frag_shader)
    # cam.stateSet.setAttributeAndModes(prog, osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE)
    cam.stateSet.attributes.append((prog, osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE))
    # cam.stateSet.setTextureAttributeAndModes(0, input_tex, osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE)
    cam.stateSet.textureAttributes[0] = (input_tex, osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE)
    cam.stateSet.uniforms["inputTex"] = 0

    if extra_textures:
        for unit, uname, tex in extra_textures:
            # cam.stateSet.setTextureAttributeAndModes(unit, tex, osg.StateAttribute.ON)
            cam.stateSet.textureAttributes[unit] = tex
            cam.stateSet.uniforms[uname] = unit

    if extra_uniforms:
        for k, v in extra_uniforms.items():
            cam.stateSet.uniforms[k] = v

    cam.children.append(make_fullscreen_quad())
    return cam

def make_blur_pass(input_tex, output_tex, w, h, direction, name, order):
    step = osg.Vec2(1.0 / w, 0.0) if direction == "horizontal" else osg.Vec2(0.0, 1.0 / h)
    return make_fullscreen_rtt_pass(
        input_tex, output_tex, BLUR_FRAG, w, h,
        name=name, order=order,
        extra_uniforms={"texelStep": step},
    )

def make_composite(glow_tex, ent_blur_tex, hud_tex, hud_blur_tex, w, h):
    cam = osg.Camera()
    cam.name             = "Composite"
    cam.renderOrder      = osg.Camera.POST_RENDER
    cam.clearMask        = GL_DEPTH_BUFFER_BIT
    cam.viewport         = osg.Viewport(0, 0, w, h)
    cam.projectionMatrix = osg.Matrix.identity()
    cam.viewMatrix       = osg.Matrix.identity()
    cam.referenceFrame   = osg.Transform.ABSOLUTE_RF
    cam.allowEventFocus  = False

    prog = make_program("composite_prog", FULLSCREEN_VERT, COMPOSITE_FRAG)
    ss = cam.stateSet
    # ss.setAttributeAndModes(prog, osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE)
    ss.attributes.append((prog, osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE))
    # ss.setTextureAttributeAndModes(0, glow_tex,      osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE)
    # ss.setTextureAttributeAndModes(1, ent_blur_tex,  osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE)
    # ss.setTextureAttributeAndModes(2, hud_tex,       osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE)
    # ss.setTextureAttributeAndModes(3, hud_blur_tex,  osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE)
    ss.textureAttributes[0] = (glow_tex,      osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE)
    ss.textureAttributes[1] = (ent_blur_tex,  osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE)
    ss.textureAttributes[2] = (hud_tex,       osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE)
    ss.textureAttributes[3] = (hud_blur_tex,  osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE)
    ss.uniforms["glowTex"]         = 0
    ss.uniforms["entBlurTex"]      = 1
    ss.uniforms["hudTex"]          = 2
    ss.uniforms["hudBlurTex"]      = 3
    ss.uniforms["glowStrength"]    = 2.5
    ss.uniforms["hudGlowStrength"] = 1.5

    cam.children.append(make_fullscreen_quad())
    return cam

# ---------------------------------------------------------------------------
# Scene builders
# ---------------------------------------------------------------------------

class GeometryCollector(osg.NodeVisitor):
    def __init__(self):
        super().__init__(osg.NodeVisitor.TraversalMode.TRAVERSE_ALL_CHILDREN)
        self.geometries = []

    def apply(self, node):
        if isinstance(node, osg.Geometry):
            self.geometries.append(node)
        self.traverse(node)

def create_enterprise(path):
    node    = osgDB.readNodeFile(path)
    wrapper = osg.Group()
    wrapper.children.append(node)

    prog = make_program("hologram_prog", HOLOGRAM_VERT, HOLOGRAM_FRAG)
    # wrapper.stateSet.setAttributeAndModes(
    wrapper.stateSet.attributes.append((
        prog,
        osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE,
    ))

    wrapper.stateSet.uniforms["hudColor"]       = osg.Vec3(0.2, 0.8, 1.0)
    wrapper.stateSet.uniforms["rimPower"]       = 3.0
    wrapper.stateSet.uniforms["scanIndex"]      = 0.0
    wrapper.stateSet.uniforms["scanWidth"]      = 32.0
    wrapper.stateSet.uniforms["scanPulse"]      = 0.0
    wrapper.stateSet.uniforms["highlightColor"] = osg.Vec3(1.0, 0.38, 0.03)

    collector = GeometryCollector()
    node.accept(collector)

    for i, geom in enumerate(collector.geometries):
        geom.stateSet.uniforms["geometryIndex"] = float(i)

    geometry_count = len(collector.geometries)
    print(f"Enterprise: {geometry_count} geometries")

    return wrapper, geometry_count

def _text_start_frac(label, font_size, path, slug_atlas):
    """Center label on path by computing its total advance width."""
    total_advance = 0.0

    for c in label:
        info = slug_atlas.get_shape(slughorn.Key(ord(c)))
        adv  = (info.advance if info and info.advance > 0 else 0.6) * font_size
        total_advance += adv

    arc_len = path.arc_length()
    return max(0.0, 0.5 - total_advance / (2.0 * arc_len)) if arc_len > 0 else 0.0


def create_shield_hud():
    """Four labeled shield sectors in canvas [0,1] space; positioned by create_ring_transform."""
    slug_atlas = slughorn.Atlas()
    canvas     = slughorn.canvas.Canvas(slug_atlas, slughorn.KeyIterator())
    canvas.decomposer().tolerance = slughorn.TOLERANCE_BALANCED

    # Load Orbitron glyphs for all uppercase letters we need.
    labels     = "".join(s["label"] for s in SECTORS)
    codepoints = list({ord(c) for c in labels})
    metrics    = slughorn.freetype.load_font_metrics(FONT_PATH)
    slughorn.freetype.load_font_glyphs(FONT_PATH, codepoints, slug_atlas)

    pivot = slughorn.ShapeInfo.Origin(CX, CY)

    # ── Sector arcs + labels ──────────────────────────────────────────────────
    for sec in SECTORS:
        c     = sec["center"]
        start = c - SECTOR_HALF
        end   = c + SECTOR_HALF

        # Filled annular sector: outer arc → line inward → inner arc back → close.
        canvas.begin_path()
        canvas.arc(CX, CY, ARC_RADIUS, start, end)
        canvas.line_to(CX + FILL_RADIUS * math.cos(end),
                       CY + FILL_RADIUS * math.sin(end))
        canvas.arc(CX, CY, FILL_RADIUS, end, start, True)
        canvas.close_path()
        canvas.fill(FILL_COLOR, 1.0, slughorn.Key(sec["name"] + "_fill"), pivot)

        # Outer arc stroke (drawn on top of fill).
        canvas.begin_path()
        canvas.arc(CX, CY, ARC_RADIUS, start, end)
        canvas.stroke(ARC_WIDTH, BASE_COLOR, 1.0,
                      slughorn.Key(sec["name"]), pivot)

        # Text label on a larger-radius arc, centered.
        text_path = slughorn.canvas.Path()
        text_path.arc(CX, CY, TEXT_RADIUS, start, end)

        sf = _text_start_frac(sec["label"], FONT_SIZE, text_path, slug_atlas)
        canvas.text_on_path(text_path, sec["label"], FONT_SIZE, sf,
                            TEXT_COLOR, metrics,
                            slughorn.canvas.TextAnchorY.CAP_CENTER)

        canvas.finalize(slughorn.Key(sec["name"] + "_comp"))

    # ── Divider ticks ─────────────────────────────────────────────────────────
    for angle in TICK_ANGLES:
        cos_a, sin_a = math.cos(angle), math.sin(angle)
        canvas.begin_path()
        canvas.move_to(CX + TICK_INNER * cos_a, CY + TICK_INNER * sin_a)
        canvas.line_to(CX + TICK_OUTER * cos_a, CY + TICK_OUTER * sin_a)
        canvas.stroke(TICK_WIDTH, BASE_COLOR, 1.0,
                      slughorn.Key(f"tick_{angle:.3f}"), pivot)

    canvas.finalize(slughorn.Key("ticks_comp"))

    slug_atlas.build()

    atlas = osgSlug.Atlas(slug_atlas)

    atlas.packTextures()

    sd = osgSlug.ShapeDrawable()

    atlas.children.append(sd)

    # Add all layers from every composite (fill + stroke + text glyphs per sector).
    comp_keys = [slughorn.Key(s["name"] + "_comp") for s in SECTORS]
    comp_keys.append(slughorn.Key("ticks_comp"))

    for key in comp_keys:
        comp = slug_atlas.get_composite_shape(key)
        if comp:
            for layer in comp.layers:
                sd.addLayer(layer)

    sd.compile()

    r = TEXT_RADIUS * 1.5
    sd.initialBound = osg.BoundingBoxf(
        osg.Vec3f(CX - r, CY - r, -r),
        osg.Vec3f(CX + r, CY + r,  r),
    )

    # geode          = osg.Geode(drawables=[sd])
    # geode.stateSet = atlas.createDefaultStateSet(
    #     **{"vertEffects": VERT_EFFECTS} if HUD_EFFECTS else {}
    # )

    # return geode, sd
    return atlas, sd

def create_ring_transform(enterprise_wrapper):
    """Builds a MatrixTransform that places the ring coplanar with the saucer.

    The saucer is naturally in the XY plane under OSG's default orientation,
    so no rotation is needed — just scale, center, and translate to the
    bounding sphere center.
    """
    bsphere = enterprise_wrapper.bound
    center  = bsphere.center
    radius  = bsphere.radius

    S = radius / ARC_RADIUS

    print(f"Bounding sphere: center={center}, radius={radius:.3f}  →  ring scale={S:.3f}")

    mt = osg.MatrixTransform()
    mt.matrix = (
        osg.Matrix.scale(S, S, S) *
        osg.Matrix.translate(-CX * S, -CY * S, 0.0) *
        osg.Matrix.translate(center)
    )

    return mt

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    model_path = sys.argv[1] if len(sys.argv) > 1 else "enterprise.osgt"

    # Enterprise subgraph textures
    entColor  = make_color_texture(W, H)
    entDepth  = make_depth_texture(W, H)
    edgeTex   = make_color_texture(W, H)
    entBlurA  = make_color_texture(W, H)
    entBlurB  = make_color_texture(W, H)

    # HUD subgraph textures (own bloom pipeline)
    hudColor  = make_color_texture(W, H)
    hudDepth  = make_depth_texture(W, H)
    hudBlurA  = make_color_texture(W, H)
    hudBlurB  = make_color_texture(W, H)

    for tex in (entColor, entDepth, edgeTex, entBlurA, entBlurB,
                hudColor, hudDepth, hudBlurA, hudBlurB):
        tex.dataVariance = osg.Object.DYNAMIC

    enterprise, geometry_count = create_enterprise(model_path)
    hud_geode, sd              = create_shield_hud()

    ring_mt = create_ring_transform(enterprise)
    ring_mt.children.append(hud_geode)

    # Enterprise and HUD rendered into separate RTT cameras so they have
    # independent bloom/blur pipelines.
    ent_pass = make_scene_rtt_pass(entColor, entDepth, enterprise, W, H,
                                   name="Enterprise RTT", order=0)
    ent_pass.cullingActive = False

    hud_pass = make_scene_rtt_pass(hudColor, hudDepth, ring_mt, W, H,
                                   name="HUD RTT", order=1)
    hud_pass.cullingActive = False

    edge_pass = make_fullscreen_rtt_pass(
        input_tex   = entColor,
        output_tex  = edgeTex,
        frag_shader = EDGE_FRAG,
        w=W, h=H, name="Edge RTT", order=2,
        extra_textures = [(1, "depthTex", entDepth)],
        extra_uniforms = {
            "znear":     0.0,
            "zfar":      1.0,
            "texelSize": osg.Vec2(4.0 / W, 4.0 / H),
            "hudColor":  osg.Vec3(0.2, 0.8, 1.0),
        },
    )

    ent_blur_h = make_blur_pass(edgeTex,  entBlurA, W, H, "horizontal", "Ent Blur H RTT", order=3)
    ent_blur_v = make_blur_pass(entBlurA, entBlurB, W, H, "vertical",   "Ent Blur V RTT", order=4)

    hud_blur_h = make_blur_pass(hudColor, hudBlurA, W, H, "horizontal", "HUD Blur H RTT", order=5)
    hud_blur_v = make_blur_pass(hudBlurA, hudBlurB, W, H, "vertical",   "HUD Blur V RTT", order=6)

    composite = make_composite(edgeTex, entBlurB, hudColor, hudBlurB, W, H)

    composite.stateSet.uniforms["hudGlowStrength"] = 1.5 if HUD_EFFECTS else 0.0

    root = osg.Group()
    root_children = [ent_pass, hud_pass, edge_pass, ent_blur_h, ent_blur_v]

    if HUD_EFFECTS:
        root_children += [hud_blur_h, hud_blur_v]

    root.children.extend(root_children + [composite])

    viewer = osgViewer.Viewer(osg.ArgumentParser("enterprise-hud3d.py", ("tmp", "--clear-color", "0.1,0.1,0.1")))
    viewer.sceneData         = root
    # viewer.sceneData         = scene_group
    viewer.cameraManipulator = osgGA.TrackballManipulator()
    viewer.camera.clearColor = osg.Vec4(0, 0, 0, 1)

    def update_near_far(ri):
        _, _, near, far = ri.state.projectionMatrix.getPerspective()
        edge_pass.stateSet.uniforms["znear"] = float(near)
        edge_pass.stateSet.uniforms["zfar"]  = float(far)

    viewer.camera.preDrawCallback = update_near_far

    t0 = time.time()

    while not viewer.done:
        elapsed = time.time() - t0

        if geometry_count > 0:
            scan_speed = 45.0
            scan_index = (elapsed * scan_speed) % float(geometry_count)
            scan_pulse = 0.5 + 0.5 * math.sin(elapsed * math.tau * 3.0)

            enterprise.stateSet.uniforms["scanIndex"] = float(scan_index)
            enterprise.stateSet.uniforms["scanPulse"] = float(scan_pulse)

        viewer.frame()
