#include "osgSlug/Drawable/ShapeDrawable.hpp"

#include "Drawable/Util.hpp"

#include <osg/BufferIndexBinding>
#include <osg/BufferObject>
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

// mask is never null in practice (callers pass either a real RenderGroup mask or the Atlas's
// own null sentinel) -- every draw call must leave something valid bound at
// RENDER_MASK_UBO_BINDING now that osgSlug_FragmentMask() reads it unconditionally, not just
// when a mask-aware hook opts in. The guard only covers the (should-never-happen) case of a
// drawable with no Atlas parent.
static void applyMask(osg::State& state, const RenderMask* mask) {
	if(mask) mask->apply(state);
}

// Zero-filled 7-Vec4 slice: reserved for layers that produce no geometry (invisible, or shape
// lookup failed) so their SSBO slot still exists at the right position -- lidx (== _layers index
// + 1) always addresses a valid buffer, with no separate "skip counter" that can drift out of
// sync with the buffer's actual layout.
static void pushEmptySlot(osgx::Vec4Array& buf) {
	buf.append_range({
		Vec4(0_cv, 0_cv, 0_cv, 0_cv),
		Vec4(0_cv, 0_cv, 0_cv, 0_cv),
		Vec4(0_cv, 0_cv, 0_cv, 0_cv),
		Vec4(0_cv, 0_cv, 0_cv, 0_cv),
		Vec4(0_cv, 0_cv, 0_cv, 0_cv),
		Vec4(0_cv, 0_cv, 0_cv, 0_cv),
		Vec4(0_cv, 0_cv, 0_cv, 0_cv)
	});
}

} // anonymous namespace

void ShapeDrawable::addLayer(const slughorn::Layer& layer) {
	_layers.push_back({layer, nullptr, nullptr});
}

void ShapeDrawable::addCompositeShape(const slughorn::CompositeShape& composite) {
	osg::ref_ptr<RenderMask> mask;

	if(composite.mask && !composite.layers.empty()) {
		mask = new RenderMask(*composite.mask, RENDER_MASK_UBO_BINDING);
	}

	for(const auto& layer : composite.layers) _layers.push_back({layer, nullptr, mask});
}

std::vector<slughorn::Layer> ShapeDrawable::getLayers() const {
	std::vector<slughorn::Layer> result;

	result.reserve(_layers.size());

	for(const auto& rs : _layers) result.push_back(rs.layer);

	return result;
}

osg::BoundingBox ShapeDrawable::computeBoundingBox() const {
	osg::BoundingBox bb;

	const auto* verts = dynamic_cast<const osgx::Vec4Array*>(getVertexAttribArray(0));

	if(verts) for(const auto& v : *verts) bb.expandBy(Vec3(v.x(), v.y(), v.z()));

	// Baked vertices are the TRUE authored quads; layer.bleed is rendered content pushed
	// outward on the GPU, so culling must account for it here. (The ~pixel AA margin is NOT
	// included: coverage past the true edge is zero, so nothing visible is ever culled away.)
	slug_t bleed = 0_cv;

	for(const auto& rs : _layers) bleed = std::max(bleed, rs.layer.bleed * rs.layer.scale);

	if(bb.valid() && bleed > 0_cv) {
		bb.xMin() -= bleed;
		bb.yMin() -= bleed;
		bb.xMax() += bleed;
		bb.yMax() += bleed;
	}

	return bb;
}

void ShapeDrawable::drawImplementation(osg::RenderInfo& renderInfo) const {
	// Fast path: single SrcOver, unmasked group - no state changes needed. The ambient default
	// StateSet (Atlas::createDefaultStateSet()) already has SrcOver blend AND the Atlas's null
	// mask bound at RENDER_MASK_UBO_BINDING, so this is still safe now that every fragment
	// shader reads osgSlug_mask unconditionally.
	if(_groups.size() == 1 && _groups[0].blendMode == slughorn::BlendMode::SrcOver && !_groups[0].mask) {
		osg::Geometry::drawImplementation(renderInfo);

		return;
	}

	osg::State& state = *renderInfo.getState();
	auto* atlas = getAtlas();
	RenderMask* nullMask = atlas ? atlas->getNullMask() : nullptr;

	bool usingVBOs = state.useVertexBufferObject(_supportsVertexBufferObjects && _useVertexBufferObjects);
	bool usingVAOs = usingVBOs && state.useVertexArrayObject(_useVertexArrayObject);
	auto* vas = state.getCurrentVertexArrayState();

	vas->setVertexBufferObjectSupported(usingVBOs);

	// Bind all vertex arrays and element buffer objects once.
	drawVertexArraysImplementation(renderInfo);

	// One draw call per group with the appropriate blend state and mask binding. Every group
	// binds SOMETHING at RENDER_MASK_UBO_BINDING -- the group's own mask, or the null sentinel --
	// there is no more "leave it unbound" state.
	for(const auto& g : _groups) {
		applyBlendMode(state, g.blendMode);
		applyMask(state, g.mask ? g.mask.get() : nullMask);

		g.indices->draw(state, usingVBOs);
	}

	// Restore defaults so we don't leak into subsequent drawables: SrcOver blend state, and the
	// null mask (replaces the old unbindMask() -- binding the sentinel instead of unbinding is
	// what makes reading osgSlug_mask.type unconditionally in main() well-defined).
	applyBlendMode(state, slughorn::BlendMode::SrcOver);
	applyMask(state, nullMask);

	if(usingVBOs && !usingVAOs) {
		vas->unbindVertexBufferObject();
		vas->unbindElementBufferObject();
	}
}

void ShapeDrawable::bindSSBOAttribs(osgx::Vec4Array* verts, osgx::Vec4Array* emCoords) {
	setVertexAttribArray(0, verts); setVertexAttribBinding(0, osg::Geometry::BIND_PER_VERTEX);
	setVertexAttribArray(1, emCoords); setVertexAttribBinding(1, osg::Geometry::BIND_PER_VERTEX);
}

void ShapeDrawable::compile() {
	if(_compiled) return;

	auto* atlas = getAtlas();

	if(!atlas) {
		OSG_WARN
			<< "ShapeDrawable::compile(): no Atlas parent -- "
			<< "add to an Atlas node first (e.g. atlas->addChild(drawable))" << std::endl
		;

		return;
	}

	if(!atlas->isBuilt() || _layers.empty()) return;

	auto vertices = osgx::make_ref<osgx::Vec4Array>();
	auto emCoords = osgx::make_ref<osgx::Vec4Array>();

	_groups.clear();

	osg::ref_ptr<index_type> groupIndices;
	slughorn::BlendMode groupBlend = slughorn::BlendMode::SrcOver;
	osg::ref_ptr<RenderMask> groupMask;

	auto flushGroup = [&]() {
		if(groupIndices && !groupIndices->empty())
			_groups.push_back({groupBlend, slughorn::DrawMode::Visible, groupIndices, groupMask});
	};

	// Layer SSBO (binding 1): one Vec4Array per _layers entry, each holding 7 vec4s.
	// All arrays share a single ShaderStorageBufferObject so the GPU sees one contiguous buffer,
	// but each array has its own modifiedCount, enabling per-layer dirty uploads.
	//
	// [0] color: RGBA
	// [1] gradientMeta: x=gradientId, yz=center, w=r0_norm
	// [2] gradientXform
	// [3] effectData: x=effectId, y=shapeIndex, z=msdfData, w=effectParam
	// [4] transformData: xy=layer.transform.xy (canvas-space origin, read by osgSlug_Mask_Evaluate); z=layer.bleed; w=unused
	// [5] axisX: xyz=model-space dir of +1 em along X, w=worldPerEm rate
	// [6] axisY: xyz=model-space dir of +1 em along Y, w=worldPerEm rate
	auto ssbo = osgx::make_ref<osg::ShaderStorageBufferObject>();

	index_element_type base = 0;
	RenderMask* lastRepacked = nullptr; // dedupes repack() across consecutive shared-mask layers

	for(size_t i = 0; i < _layers.size(); i++) {
		auto& rs = _layers[i];
		const auto& layer = rs.layer;
		const slug_t lidx = cv(i + 1);

		auto layerBuf = osgx::make_ref<osgx::Vec4Array>();
		const bool visible = layer.drawMode == slughorn::DrawMode::Visible;
		const auto shape = atlas->getShape(layer.key);

		if(visible && shape) {
			if(!groupIndices || layer.blendMode != groupBlend || rs.mask.get() != groupMask.get()) {
				flushGroup();
				groupIndices = osgx::make_ref<index_type>();
				groupBlend = layer.blendMode;
				groupMask = rs.mask;
			}

			// TRUE authored quad + em bounds - never padded. The AA margin and layer.bleed are
			// pushed outward live in the vertex stage (SHADER_VERT), so these baked coordinates
			// stay exactly what the user authored and anchoring can never drift.
			const auto q = shape->computeQuad(layer.transform, layer.scale);
			const slug_t z = cv(layer.transform.z);

			vertices->append_range({
				{q.x0, q.y0, z, lidx},
				{q.x1, q.y0, z, lidx},
				{q.x1, q.y1, z, lidx},
				{q.x0, q.y1, z, lidx}
			});

			const auto [emX0, emY0, emX1, emY1] = computeEmBounds(*shape);

			emCoords->append_range({
				{emX0, emY0, 0_cv, 0_cv},
				{emX1, emY0, 1_cv, 0_cv},
				{emX1, emY1, 1_cv, 1_cv},
				{emX0, emY1, 0_cv, 1_cv}
			});

			const auto [gmeta, gxform] = buildGradientData(*atlas, layer);
			const slug_t shapeIdx = cv(atlas->getShapeIndex(layer.key));

			layerBuf->push_back({layer.color.r, layer.color.g, layer.color.b, layer.color.a});
			layerBuf->push_back(gmeta);
			layerBuf->push_back(gxform);
			layerBuf->push_back({
				cv(layer.effectId),
				shapeIdx,
				cv(packMSDFData(shape->msdfLayer, shape->msdfRange)),
				layer.effectParam
			});
			layerBuf->push_back({layer.transform.x, layer.transform.y, layer.bleed, 0_cv});
			// [5]/[6] quad frame: xyz = model-space direction of +1 em along each quad axis,
			// w = worldPerEm rate along it. SHADER_VERT's live margin/bleed push uses these
			// (a hook can override them when it deforms geometry non-uniformly).
			layerBuf->push_back({1_cv, 0_cv, 0_cv, layer.scale});
			layerBuf->push_back({0_cv, 1_cv, 0_cv, layer.scale});

			if(rs.mask && rs.mask.get() != lastRepacked) {
				rs.mask->repack(*atlas);
				lastRepacked = rs.mask.get();
			}

			groupIndices->append_range({
				base, index_element_type(base + 1), index_element_type(base + 2),
				base, index_element_type(base + 2), index_element_type(base + 3)
			});

			base += 4;
		}
		else {
			pushEmptySlot(*layerBuf);
		}

		layerBuf->setBufferObject(ssbo);
		rs.buffer = layerBuf;
	}

	flushGroup();

	if(vertices->empty()) return;

	bindSSBOAttribs(vertices, emCoords);

	for(const auto& g : _groups) addPrimitiveSet(g.indices);

	const auto totalSize = static_cast<GLsizeiptr>(_layers.size() * 7 * sizeof(Vec4));

	getOrCreateStateSet()->setAttributeAndModes(
		new osg::ShaderStorageBufferBinding(1, _layers[0].buffer, 0, totalSize),
		osg::StateAttribute::ON
	);

	_compiled = true;
}

void ShapeDrawable::setLayerColor(size_t index, const slughorn::Color& color) {
	if(index >= _layers.size() || !_layers[index].buffer) {
		if(!_compiled)
			OSG_WARN << "ShapeDrawable::setLayerColor(): called before compile() -- call ignored" << std::endl;
		return;
	}

	auto& rs = _layers[index];

	rs.layer.color = color;
	(*rs.buffer)[0] = Vec4(color.r, color.g, color.b, color.a);
}

void ShapeDrawable::setLayerEffectId(size_t index, uint32_t effectId) {
	if(index >= _layers.size() || !_layers[index].buffer) {
		if(!_compiled)
			OSG_WARN << "ShapeDrawable::setLayerEffectId(): called before compile() -- call ignored" << std::endl;
		return;
	}

	auto& rs = _layers[index];

	rs.layer.effectId = effectId;
	(*rs.buffer)[3].x() = cv(effectId);
}

void ShapeDrawable::setLayerEffectParam(size_t index, slug_t param) {
	if(index >= _layers.size() || !_layers[index].buffer) {
		if(!_compiled)
			OSG_WARN << "ShapeDrawable::setLayerEffectParam(): called before compile() -- call ignored" << std::endl;
		return;
	}

	auto& rs = _layers[index];

	rs.layer.effectParam = param;
	(*rs.buffer)[3].w() = param;
}

void ShapeDrawable::setLayerShapeIndex(size_t index, size_t shapeIndex) {
	if(index >= _layers.size() || !_layers[index].buffer) return;

	(*_layers[index].buffer)[3].y() = cv(shapeIndex);
}

void ShapeDrawable::setLayerGradientTransform(size_t index, const slughorn::Matrix& m) {
	auto* atlas = getAtlas();

	if(index >= _layers.size() || !_layers[index].buffer || !atlas) return;

	const auto& layer = _layers[index].layer;

	if(layer.gradientId <= 0) return;

	slughorn::GradientInfo tmp = atlas->getGradients()[layer.gradientId - 1];
	tmp.transform = m;

	const auto [gmeta, gxform] = buildGradientDataFromInfo(layer.gradientId, tmp);
	auto& buf = *_layers[index].buffer;

	buf[1] = gmeta;
	buf[2] = gxform;
}

void ShapeDrawable::updateLayer(size_t index, const slughorn::Layer& layer) {
	auto* atlas = getAtlas();

	if(index >= _layers.size() || !_layers[index].buffer || !atlas) return;

	_layers[index].layer = layer;

	const auto [gmeta, gxform] = buildGradientData(*atlas, layer);
	const slug_t shapeIdx = cv(atlas->getShapeIndex(layer.key));

	auto& buf = *_layers[index].buffer;

	buf[0] = Vec4(layer.color.r, layer.color.g, layer.color.b, layer.color.a);
	buf[1] = gmeta;
	buf[2] = gxform;
	buf[3] = Vec4(cv(layer.effectId), shapeIdx, buf[3].z(), buf[3].w());
	buf[4] = Vec4(layer.transform.x, layer.transform.y, 0_cv, 0_cv);
}

void ShapeDrawable::dirtyLayers() {
	for(auto& rs : _layers) if(rs.buffer) rs.buffer->dirty();
}

void ShapeDrawable::dirtyLayers(size_t index) {
	if(index < _layers.size() && _layers[index].buffer) _layers[index].buffer->dirty();
}

}
