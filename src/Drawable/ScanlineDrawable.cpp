#include "osgSlug/Drawable/ScanlineDrawable.hpp"

#include "Drawable/Util.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/BlendFunc>
#include <osg/Depth>

OSGSLUG_ENABLE_WARNINGS

namespace osgSlug {

void ScanlineDrawable::addCompositeShape(const slughorn::CompositeShape& cs) {
	for(const auto& layer : cs.layers) _composite.layers.push_back(layer);
}

void ScanlineDrawable::compile() {
	if(_compiled) return;

	auto* atlas = getAtlas();

	if(!atlas || !atlas->isBuilt()) return;

	auto* scanlineTex = atlas->getScanlineTexture();

	if(!scanlineTex) {
		OSG_WARN
			<< "ScanlineDrawable: scanline texture unavailable; was packTextures() called?"
			<< std::endl
		;

		return;
	}

	auto verts = osgx::make_ref<osgx::Vec4Array>();
	auto emCoords = osgx::make_ref<osgx::Vec4Array>();
	auto curveRanges = osgx::make_ref<osgx::Vec4Array>(); // x=curveStart, y=curveCount
	auto colors = osgx::make_ref<osgx::Vec4Array>(); // per-layer RGBA from layer.color

	_bbox.init();

	for(const auto& layer : _composite.layers) {
		const auto shape = atlas->getShape(layer.key);

		if(!shape || !shape->scanlineCurveCount) continue;

		const auto [x0, y0, x1, y1] = computeEmBounds(*shape, 0_cv);

		const slug_t s = layer.scale;
		const slug_t tx = layer.transform.x;
		const slug_t ty = layer.transform.y;

		// World positions = (glyph_em_coord + advance_offset) * scale.
		// emCoords stay in glyph units (no offset, no scale) to match the curve texture.
		const slug_t wx0 = (x0 + tx) * s;
		const slug_t wy0 = (y0 + ty) * s;
		const slug_t wx1 = (x1 + tx) * s;
		const slug_t wy1 = (y1 + ty) * s;

		const slug_t cs = cv(shape->scanlineCurveStart);
		const slug_t cc = cv(shape->scanlineCurveCount);
		const Vec4 lc = {layer.color.r, layer.color.g, layer.color.b, layer.color.a};

		// If layer has no color set, fall back to the drawable-level _color.
		const Vec4 fc = (lc.w() > 0_cv) ? lc : _color;

		// Two triangles per quad (GL_TRIANGLES): BL, BR, TR, BL, TR, TL.
		auto push = [&](slug_t wx, slug_t wy, slug_t ex, slug_t ey) {
			verts->push_back({wx, wy, 0_cv, 1_cv});
			emCoords->push_back({ex, ey, 0_cv, 0_cv});
			curveRanges->push_back({cs, cc, 0_cv, 0_cv});
			colors->push_back(fc);
		};

		push(wx0, wy0, x0, y0); // BL
		push(wx1, wy0, x1, y0); // BR
		push(wx1, wy1, x1, y1); // TR
		push(wx0, wy0, x0, y0); // BL
		push(wx1, wy1, x1, y1); // TR
		push(wx0, wy1, x0, y1); // TL

		_bbox.expandBy(Vec3(wx0, wy0, -0.1_cv));
		_bbox.expandBy(Vec3(wx1, wy1, 0.1_cv));
	}

	if(verts->empty()) {
		OSG_WARN
			<< "ScanlineDrawable: no renderable layers (zero curves or missing shapes)"
			<< std::endl
		;

		return;
	}

	setVertexAttribArray(0, verts); setVertexAttribBinding(0, osg::Geometry::BIND_PER_VERTEX);
	setVertexAttribArray(1, emCoords); setVertexAttribBinding(1, osg::Geometry::BIND_PER_VERTEX);
	setVertexAttribArray(2, curveRanges); setVertexAttribBinding(2, osg::Geometry::BIND_PER_VERTEX);
	setVertexAttribArray(3, colors); setVertexAttribBinding(3, osg::Geometry::BIND_PER_VERTEX);
	setUseVertexBufferObjects(true);

	removePrimitiveSet(0, getNumPrimitiveSets());
	addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts->size())));

	setInitialBound(_bbox);

	auto* prog = new osg::Program();

	prog->addShader(new osg::Shader(osg::Shader::VERTEX, Atlas::SHADER_SCANLINE_VERT));
	prog->addShader(new osg::Shader(osg::Shader::FRAGMENT, Atlas::SHADER_SCANLINE_FRAG));

	auto* ss = new osg::StateSet();

	ss->setAttributeAndModes(prog, osg::StateAttribute::ON);
	ss->setTextureAttributeAndModes(0, scanlineTex, osg::StateAttribute::ON);
	ss->addUniform(new osg::Uniform("u_scanlineTex", 0));
	ss->addUniform(new osg::Uniform("u_texWidth", static_cast<int>(atlas->getTextureWidth())));
	ss->setAttributeAndModes(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
	ss->setMode(GL_BLEND, osg::StateAttribute::ON);
	ss->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
	ss->setAttributeAndModes(new osg::Depth(osg::Depth::LESS, 0.0, 1.0, false));

	_compiled = true;

	setStateSet(ss);
}

void ScanlineDrawable::drawImplementation(osg::RenderInfo& renderInfo) const {
	osg::Geometry::drawImplementation(renderInfo);
}

osg::BoundingBox ScanlineDrawable::computeBoundingBox() const {
	return _bbox;
}

}
