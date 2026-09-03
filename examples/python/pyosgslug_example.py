#!/usr/bin/env python3

# Shared helpers for the pyosgslug-*.py examples -- mirrors
# ~/dev/OpenSceneGraph.py/examples/pyosg_example.py's pattern: a plain importable module (no
# class hierarchy), grown incrementally as more examples need shared bits, not a speculative
# framework built up front.
#
# Deliberately does NOT import OpenSceneGraph at module top, matching pyosg_example.py's own
# reasoning: this keeps the module side-effect-free to import at any point relative to `from
# OpenSceneGraph import *` (window_size()'s env-var defaults are the one place that actually
# matters -- see below). make_trackball() imports osg/osgGA locally instead.

import os

# setdefault(), not update() -- an example that already set its own OSG_WINDOW/OSG_THREADING/
# etc. before importing this keeps what it set; this only fills in whatever it didn't.
os.environ.setdefault("OSG_WINDOW", "50 50 800 600")
os.environ.setdefault("OSG_THREADING", "SingleThreaded")
os.environ.setdefault("OSG_GL_CONTEXT_PROFILE_MASK", "1")
os.environ.setdefault("OSG_GL_VERSION", "4.6")
os.environ.setdefault("OSG_GL_CONTEXT_VERSION", "4.6")

# Derives (width, height) from OSG_WINDOW ("x y width height") instead of a second,
# separately-hardcoded W, H constant per example.
def window_size(default=(800, 600)):
	spec = os.environ.get("OSG_WINDOW")

	if not spec:
		return default

	x, y, w, h = spec.split()

	return int(w), int(h)

# Y-up, XY-plane version of makeTrackball() in osgslug-example.hpp. Plain
# osgGA.TrackballManipulator() assumes Z-up content and frames anything authored in slughorn's
# native XY plane edge-on by default -- this positions the eye along +Z looking down at the
# content face-on instead, with +Y as up.
def make_trackball(scene):
	from OpenSceneGraph import osg, osgGA

	bound = scene.bound
	center = osg.Vec3d(bound.center) if bound.valid() else osg.Vec3d(0.0, 0.0, 0.0)
	radius = max(bound.radius, 1e-3) if bound.valid() else 1.0
	m = osgGA.TrackballManipulator()

	m.homePosition = (
		center + osg.Vec3d(0.0, 0.0, radius * 3.5),
		center,
		osg.Vec3d(0.0, 1.0, 0.0)
	)

	return m
