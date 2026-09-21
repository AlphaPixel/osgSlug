#!/usr/bin/env python3

# Shared helpers for the pyosgslug-*.py examples - mirrors
# ~/dev/OpenSceneGraph.py/examples/pyosg_example.py's pattern: a plain importable module (no
# class hierarchy), grown incrementally as more examples need shared bits, not a speculative
# framework built up front.
#
# Imports OpenSceneGraph after installing its environment defaults below: shared event handlers
# must subclass its concrete types, so the module now needs those names at definition time.

import argparse
import os

# setdefault(), not update() - an example that already set its own OSG_WINDOW/OSG_THREADING/
# etc. before importing this keeps what it set; this only fills in whatever it didn't.
os.environ.setdefault("OSG_WINDOW", "50 50 800 600")
os.environ.setdefault("OSG_THREADING", "SingleThreaded")
os.environ.setdefault("OSG_GL_CONTEXT_PROFILE_MASK", "1")
os.environ.setdefault("OSG_GL_VERSION", "4.6")
os.environ.setdefault("OSG_GL_CONTEXT_VERSION", "4.6")

from OpenSceneGraph import osg, osgGA
from OpenSceneGraph.GL import GL_MULTISAMPLE

import osgx

DEBUG_MODE_NAMES = (
	"normal",
	"checkerboard",
	"band edges",
	"quad border",
	"heatmap",
	"heatmap + grid",
	"half-white",
	"emcoord precision",
)

# Derives (width, height) from OSG_WINDOW ("x y width height") instead of a second,
# separately-hardcoded W, H constant per example.
def window_size(default=(800, 600)):
	spec = os.environ.get("OSG_WINDOW")

	if not spec:
		return default

	x, y, w, h = spec.split()

	return int(w), int(h)

# Parses the shared command-line switches supported by the Python examples. Individual examples
# with their own arguments should add them to this parser rather than building a second parser.
def parse_args(description=None, argv=None):
	parser = argparse.ArgumentParser(description=description)

	parser.add_argument(
		"--profile",
		action="store_true",
		help="show the osgx ImGui profiler and texture-inspection widget",
	)
	parser.add_argument(
		"--trackball",
		action="store_true",
		help="start with the 3D trackball manipulator instead of Ortho2DManipulator",
	)

	return parser.parse_args(argv)

# Applies shared viewer options after the caller has assigned sceneData. This starts with
# --profile and the common 2D/3D camera controls, and grows one independently-tested switch at
# a time, matching the C++ example helpers without imposing a full application framework.
def configure_viewer(viewer, scene, args):
	if args.profile:
		# Match example::run(): profiling is meaningless when presentation stalls on vblank.
		os.environ["__GL_SYNC_TO_VBLANK"] = "0"

		gui = osgx.imgui.Widget(viewer)

		gui.addProfilerSection(viewer, scene)
		gui.addTextureSection(viewer, scene)

	if args.trackball:
		viewer.cameraManipulator = make_trackball(scene)
		print("Manipulator: TrackballManipulator")

	else:
		viewer.cameraManipulator = osgx.Ortho2DManipulator()
		print("Manipulator: Ortho2DManipulator")

	viewer.eventHandlers.append(ManipulatorToggleHandler(scene))

# Y-up, XY-plane version of makeTrackball() in osgslug-example.hpp. Plain
# osgGA.TrackballManipulator() assumes Z-up content and frames anything authored in slughorn's
# native XY plane edge-on by default - this positions the eye along +Z looking down at the
# content face-on instead, with +Y as up.
def make_trackball(scene):
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

# F12 alternates between the default 2D camera and the XY-aware 3D trackball. Ortho2DManipulator
# owns an orthographic projection and disables automatic near/far computation, so returning to
# trackball must restore both a perspective projection and OSG's bounding-volume near/far policy.
class ManipulatorToggleHandler(osgGA.GUIEventHandler):
	def __init__(self, scene):
		super().__init__()

		self.scene = scene
		self.saved_projection = None

	def handle(self, ea, aa):
		if ea.handled or ea.type != osgGA.GUIEventAdapter.KEYDOWN:
			return False

		if ea.key != osgGA.GUIEventAdapter.KEY_F12:
			return False

		view = aa.view

		if view is None:
			return False

		camera = view.camera

		if isinstance(view.cameraManipulator, osgx.Ortho2DManipulator):
			camera.computeNearFarMode = (
				osg.Camera.ComputeNearFarMode.COMPUTE_NEAR_FAR_USING_BOUNDING_VOLUMES
			)

			if self.saved_projection is not None:
				camera.projectionMatrix = self.saved_projection

			else:
				viewport = camera.viewport
				aspect = viewport.width / viewport.height if viewport and viewport.height else 1.0
				camera.projectionMatrix = osg.Matrix.perspective(30.0, aspect, 1.0, 10000.0)

			view.cameraManipulator = make_trackball(self.scene)
			print("Manipulator: TrackballManipulator")

		else:
			self.saved_projection = osg.Matrix(camera.projectionMatrix)
			view.cameraManipulator = osgx.Ortho2DManipulator()
			print("Manipulator: Ortho2DManipulator")

		return True

# Shared F1-F7/debug-MSAA control, matching DebugModeHandler in
# osgslug-example.hpp. Install it on the StateSet that carries the osgSlug
# program - normally viewer.camera.stateSet - with:
#
#	viewer.eventHandlers.append(DebugModeHandler(viewer.camera.stateSet))
#
# Pressing an already-active function key returns to normal rendering; M
# toggles GL_MULTISAMPLE independently, which is useful for inspecting edge
# coverage while any debug mode is active.
class DebugModeHandler(osgGA.GUIEventHandler):
	def __init__(self, state_set):
		super().__init__()

		self.state_set = state_set
		self.mode = 0
		self.msaa = True
		self.state_set.modes[GL_MULTISAMPLE] = osg.StateAttribute.ON

		try:
			self.uniform = self.state_set.uniforms["osgSlug_debugMode"]
		except KeyError:
			self.state_set.uniforms["osgSlug_debugMode"] = 0
			self.uniform = self.state_set.uniforms["osgSlug_debugMode"]

		self.mode = self.uniform.value

	def set_mode(self, mode):
		self.mode = mode
		self.uniform.value = mode
		print(f"osgSlug_debugMode = {mode} ({DEBUG_MODE_NAMES[mode]})")

	def handle(self, ea, aa):
		if ea.handled or ea.type != osgGA.GUIEventAdapter.KEYDOWN:
			return False

		if ea.key == ord("m"):
			self.msaa = not self.msaa
			self.state_set.modes[GL_MULTISAMPLE] = (
				osg.StateAttribute.ON if self.msaa else osg.StateAttribute.OFF
			)
			print(f"MSAA: {'ON' if self.msaa else 'OFF'}")

			return True

		modes = {
			osgGA.GUIEventAdapter.KEY_F1: 1,
			osgGA.GUIEventAdapter.KEY_F2: 2,
			osgGA.GUIEventAdapter.KEY_F3: 3,
			osgGA.GUIEventAdapter.KEY_F4: 4,
			osgGA.GUIEventAdapter.KEY_F5: 5,
			osgGA.GUIEventAdapter.KEY_F6: 6,
			osgGA.GUIEventAdapter.KEY_F7: 7,
		}
		target = modes.get(ea.key)

		if target is None:
			return False

		self.set_mode(0 if self.mode == target else target)

		return True
