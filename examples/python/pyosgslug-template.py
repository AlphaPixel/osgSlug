#!/usr/bin/env python3
#vimrun! ./template.py

from OpenSceneGraph import *
from OpenSceneGraph.GL import *

import osgSlug
import slughorn

import os
import time
import math
import IPython

W, H = 800, 600

os.environ.update({
	"OSG_WINDOW": f"50 50 {W} {H}",
	"OSG_THREADING": "SingleThreaded",
	"OSG_GL_CONTEXT_PROFILE_MASK": "1",
	"OSG_GL_VERSION": "4.6",
	"OSG_GL_CONTEXT_VERSION": "4.6"
})

def create_scene_emoji():
	a = slughorn.Atlas()

	ef = slughorn.freetype.load_emoji_font(
		"font/COLRv1/NotoColorEmoji-Regular.ttf",
		[slughorn.emoji.name_to_codepoint(n) for n in ("dragon", "mage", "fairy")],
		a
	)

	a.build()

	atlas = osgSlug.Atlas(a)

	atlas.packTextures()

	sd = osgSlug.ShapeDrawable()

	sd.addCompositeShape(ef[slughorn.emoji.name_to_codepoint("mage")])

	# No Geode wrapper needed (osgSlug's Drawable IS an osg.Node in this OSG fork), and no manual
	# sd.compile() either -- atlas is already Packed at this point, so Atlas.addChild's override
	# compiles any osgSlug.Drawable child automatically.
	atlas.children.append(sd)

	return atlas

def create_scene_canvas():
	a = slughorn.Atlas()
	c = slughorn.canvas.Canvas(a)

	c.arc(0.5, 0.5, 0.4, 0.0, 2.0 * math.pi)
	c.fill(slughorn.Color(1, 0.5, 0, 1))

	cs = c.finalize()

	a.build()

	atlas = osgSlug.Atlas(a)

	atlas.packTextures()

	sd = osgSlug.ShapeDrawable()

	# TODO: Wrap `sd.compositeShapes` using `pybind11x::SequenceProxy`!
	# Syntax would then be: `sd.compositeShapes.{append,extend,...}`
	sd.addCompositeShape(cs)

	# No Geode wrapper needed (osgSlug's Drawable IS an osg.Node in this OSG fork), and no manual
	# sd.compile() either -- atlas is already Packed at this point, so Atlas.addChild's override
	# compiles any osgSlug.Drawable child automatically.
	atlas.children.append(sd)

	return atlas

if __name__ == "__main__":
	m = osg.MatrixTransform()

	m.matrix = osg.Matrix.rotate(math.radians(90.0), osg.Vec3(1.0, 0.0, 0.0))

	m.children.append(create_scene_canvas())

	viewer = osgViewer.Viewer()
	viewer.sceneData = m
	viewer.cameraManipulator = osgGA.TrackballManipulator()

	while not viewer.done:
		viewer.frame()
