#include "osgSlug/Drawable/ShapeDrawable.hpp"

#include <osg/GLExtensions>
#include <osg/RenderInfo>

namespace osgSlug {

namespace {

static void applyBlendMode(osg::State& state, slughorn::BlendMode mode) {
	using BM = slughorn::BlendMode;

	auto* ext = osg::GLExtensions::Get(state.getContextID(), true);

	// Helper: set equation to FUNC_ADD + blend func (same src/dst for RGB and alpha).
	auto bf = [&](GLenum src, GLenum dst) {
		ext->glBlendEquation(GL_FUNC_ADD);
		ext->glBlendFuncSeparate(src, dst, src, dst);
	};

	switch(mode) {
		// Premultiplied SrcOver: out = src + (1-src_alpha)*dst.
		// Requires fragment shader output to be premultiplied (rgb*a, a).
		case BM::SrcOver: bf(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); return;
		case BM::Src: bf(GL_ONE, GL_ZERO); return;
		case BM::Dst: bf(GL_ZERO, GL_ONE); return;
		case BM::SrcIn: bf(GL_DST_ALPHA, GL_ZERO); return;
		case BM::DstIn: bf(GL_ZERO, GL_SRC_ALPHA); return;
		case BM::SrcOut: bf(GL_ONE_MINUS_DST_ALPHA, GL_ZERO); return;
		case BM::DstOut: bf(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA); return;
		case BM::SrcAtop: bf(GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA); return;
		case BM::DstAtop: bf(GL_ONE_MINUS_DST_ALPHA, GL_SRC_ALPHA); return;
		case BM::Xor: bf(GL_ONE_MINUS_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA); return;
		case BM::Clear: bf(GL_ZERO, GL_ZERO); return;
		case BM::DstOver: bf(GL_ONE_MINUS_DST_ALPHA, GL_ONE); return;
		default: break;
	}

	// KHR advanced modes - GL_KHR_blend_equation_advanced enum values.
	GLenum eq = 0;

	switch(mode) {
		case BM::Multiply: eq = 0x9294; break;
		case BM::Screen: eq = 0x9295; break;
		case BM::Overlay: eq = 0x9296; break;
		case BM::Darken: eq = 0x9297; break;
		case BM::Lighten: eq = 0x9298; break;
		case BM::ColorDodge: eq = 0x9299; break;
		case BM::ColorBurn: eq = 0x929A; break;
		case BM::HardLight: eq = 0x929B; break;
		case BM::SoftLight: eq = 0x929C; break;
		case BM::Difference: eq = 0x929E; break;
		case BM::Exclusion: eq = 0x92A0; break;
		default: break;
	}

	if(!eq) return;

	if(!ext->glBlendEquation) {
		OSG_WARN
			<< "osgSlug: KHR blend mode requested but glBlendEquation unavailable; "
			<< "falling back to SrcOver." << std::endl
		;

		ext->glBlendFuncSeparate(
			GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
			GL_ONE, GL_ONE_MINUS_SRC_ALPHA
		);

		return;
	}

	ext->glBlendEquation(eq);
}

static void applyMask(osg::State& state, const RenderMask* mask) {
	if(mask) mask->apply(state);
}

// Mirrors applyBlendMode()'s "restore default after the loop" pattern -- avoids leaking a bound
// mask UBO into whatever draws next at RENDER_MASK_UBO_BINDING. Not yet load-bearing (no shader
// consumes this UBO binding today; see ai/context-todo-mask.md step 6), but cheap and correct to
// have in place before that lands.
static void unbindMask(osg::State& state) {
	auto* ext = osg::GLExtensions::Get(state.getContextID(), true);

	if(ext->glBindBufferBase) ext->glBindBufferBase(GL_UNIFORM_BUFFER, RENDER_MASK_UBO_BINDING, 0);
}

} // anonymous namespace

osg::BoundingBox ShapeDrawable::computeBoundingBox() const {
	osg::BoundingBox bb;

	const auto* verts = dynamic_cast<const osgx::Vec4Array*>(getVertexAttribArray(0));

	if(verts) for(const auto& v : *verts) bb.expandBy(Vec3(v.x(), v.y(), v.z()));

	return bb;
}

void ShapeDrawable::drawImplementation(osg::RenderInfo& renderInfo) const {
	// Fast path: single SrcOver, unmasked group - no state changes needed, base class handles it.
	if(_groups.size() == 1 && _groups[0].blendMode == slughorn::BlendMode::SrcOver && !_groups[0].mask) {
		osg::Geometry::drawImplementation(renderInfo);

		return;
	}

	osg::State& state = *renderInfo.getState();

	bool usingVBOs = state.useVertexBufferObject(_supportsVertexBufferObjects && _useVertexBufferObjects);
	bool usingVAOs = usingVBOs && state.useVertexArrayObject(_useVertexArrayObject);
	auto* vas = state.getCurrentVertexArrayState();

	vas->setVertexBufferObjectSupported(usingVBOs);

	// Bind all vertex arrays and element buffer objects once.
	drawVertexArraysImplementation(renderInfo);

	// One draw call per group with the appropriate blend state and mask binding.
	for(const auto& g : _groups) {
		applyBlendMode(state, g.blendMode);
		applyMask(state, g.mask.get());

		g.indices->draw(state, usingVBOs);
	}

	// Restore default SrcOver blend state so we don't leak into subsequent drawables.
	applyBlendMode(state, slughorn::BlendMode::SrcOver);
	unbindMask(state);

	if(usingVBOs && !usingVAOs) {
		vas->unbindVertexBufferObject();
		vas->unbindElementBufferObject();
	}
}

void ShapeDrawable::bindGL3Attribs(
	osgx::Vec4Array* verts,
	osgx::Vec4Array* colors,
	osgx::Vec4Array* emCoords,
	osgx::Vec4Array* bandXform,
	osgx::Vec4Array* shapeData,
	osgx::Vec4Array* effectData,
	osgx::Vec4Array* gradMeta,
	osgx::Vec4Array* gradXforms
) {
	setVertexAttribArray(0, verts); setVertexAttribBinding(0, osg::Geometry::BIND_PER_VERTEX);
	setVertexAttribArray(1, colors); setVertexAttribBinding(1, osg::Geometry::BIND_PER_VERTEX);
	setVertexAttribArray(2, emCoords); setVertexAttribBinding(2, osg::Geometry::BIND_PER_VERTEX);
	setVertexAttribArray(3, bandXform); setVertexAttribBinding(3, osg::Geometry::BIND_PER_VERTEX);
	setVertexAttribArray(4, shapeData); setVertexAttribBinding(4, osg::Geometry::BIND_PER_VERTEX);
	setVertexAttribArray(5, effectData);setVertexAttribBinding(5, osg::Geometry::BIND_PER_VERTEX);
	setVertexAttribArray(6, gradMeta); setVertexAttribBinding(6, osg::Geometry::BIND_PER_VERTEX);
	setVertexAttribArray(7, gradXforms);setVertexAttribBinding(7, osg::Geometry::BIND_PER_VERTEX);
}

void ShapeDrawable::bindSSBOAttribs(osgx::Vec4Array* verts, osgx::Vec4Array* emCoords) {
	setVertexAttribArray(0, verts); setVertexAttribBinding(0, osg::Geometry::BIND_PER_VERTEX);
	setVertexAttribArray(1, emCoords); setVertexAttribBinding(1, osg::Geometry::BIND_PER_VERTEX);
}

}
