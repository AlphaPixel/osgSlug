#include "osgSlug/Drawable.hpp"

#include <osg/BufferObject>
#include <osg/BufferIndexBinding>

namespace osgSlug {

namespace {

struct GradientData {
	Vec4 meta {0.0f, 0.0f, 0.0f, 0.0f};
	Vec4 xform {0.0f, 0.0f, 0.0f, 0.0f};
};

GradientData buildGradientData(const Atlas& atlas, const slughorn::Layer& layer) {
	GradientData data;

	data.meta.x() = cv(layer.gradientId);

	if(layer.gradientId <= 0) return data;

	const auto& grad = atlas.getGradients()[layer.gradientId - 1];
	const auto& m = grad.transform;

	if(grad.type == slughorn::GradientInfo::Type::Radial) {
		// Circular radial: B = invDR * I; pack column-major mat2; w = invDR > 0.
		const slug_t deltaR = cv(m.xx) - cv(grad.innerRadius);
		const slug_t invDR = deltaR > 1e-6f ? 1.0f / deltaR : 0.0f;

		data.xform = {invDR, 0.0f, 0.0f, invDR};
		data.meta = {cv(layer.gradientId), cv(m.dx), cv(m.dy), cv(grad.innerRadius) * invDR};
	}

	else if(grad.type == slughorn::GradientInfo::Type::AffineRadial) {
		// Elliptical radial: B stored in m.xx/xy/yx/yy; pack column-major mat2.
		slug_t b00 = cv(m.xx), b01 = cv(m.xy), b10 = cv(m.yx), b11 = cv(m.yy);

		if(b11 < 0.0f) { b00 = -b00; b01 = -b01; b10 = -b10; b11 = -b11; }

		data.xform = {b00, b10, b01, b11};
		data.meta = {cv(layer.gradientId), cv(m.dx), cv(m.dy), cv(grad.innerRadius)};
	}

	else if(grad.type == slughorn::GradientInfo::Type::Sweep) {
		const slug_t arcSpan = cv(m.xy);
		const slug_t invArcSpan = arcSpan > 1e-6f ? 1.0f / arcSpan : 0.0f;

		data.xform = {cv(m.dx), cv(m.dy), cv(m.xx), -invArcSpan}; // w < 0 = sweep
	}

	else data.xform = {cv(m.xx), cv(m.xy), cv(m.dx), 0.0f};

	return data;
}

// ------------------------------------------------------------------------------------------------
// SSBO mutation helpers; shared by SSBOShapeDrawable and SSBOSubdividedDrawable.
// ------------------------------------------------------------------------------------------------

// buf is the per-layer Vec4Array (4 elements); index is used only to sync _layers.
static void ssboSetLayerColor(
	osgx::Vec4Array* buf,
	std::vector<slughorn::Layer>& layers,
	size_t index,
	const slughorn::Color& color
) {
	layers[index].color = color;
	(*buf)[0] = Vec4(color.r, color.g, color.b, color.a);
}

static void ssboSetLayerEffectId(
	osgx::Vec4Array* buf,
	std::vector<slughorn::Layer>& layers,
	size_t index,
	uint32_t effectId
) {
	layers[index].effectId = effectId;
	(*buf)[3].x() = cv(effectId);
}

static void ssboSetLayerEffectParam(osgx::Vec4Array* buf, slug_t param) {
	(*buf)[3].w() = param;
}

}

ShapeDrawable::ShapeDrawable() {
	setUseDisplayList(false);
	setUseVertexBufferObjects(true);
}

osg::BoundingBox ShapeDrawable::computeBoundingBox() const {
	osg::BoundingBox bb;

	const auto* verts = dynamic_cast<const osgx::Vec4Array*>(getVertexAttribArray(0));

	if(verts) for(const auto& v : *verts) bb.expandBy(osg::Vec3(v.x(), v.y(), v.z()));

	return bb;
}

void GL3ShapeDrawable::compile() {
	if(!_atlas || !_atlas->isBuilt() || _layers.empty()) return;

	auto vertices = osgx::make_ref<osgx::Vec4Array>();
	auto colors = osgx::make_ref<osgx::Vec4Array>();
	auto emCoords = osgx::make_ref<osgx::Vec4Array>();
	auto bandXform = osgx::make_ref<osgx::Vec4Array>();
	auto shapeData = osgx::make_ref<osgx::Vec4Array>();
	auto effectData = osgx::make_ref<osgx::Vec4Array>();
	auto gradientMeta = osgx::make_ref<osgx::Vec4Array>();
	auto gradientXforms = osgx::make_ref<osgx::Vec4Array>();
	auto indices = osgx::make_ref<osgx::DrawElementsUShort>();

	// TODO (known): clear arrays here to avoid accumulation on repeated compile() calls. Safe for
	// now since compile() is only called once.

	index_element_type base = 0;

	size_t index = 0;

	for(const auto& layer : _layers) {
		const auto* shape = _atlas->getShape(layer.key);

		if(!shape) continue;

		const slug_t expand = 0.01_cv;

		// Quad placement comes entirely from slughorn truth:
		// - layer.transform.dx/dy: canvas-space origin set by decomposePath
		// - layer.scale: source units/world units
		// Scene placement is the caller's responsibility (MatrixTransform).
		// const slughorn::Quad q = shape->computeQuad(layer.transform, layer.scale, cv(expand));
		const auto q = shape->computeQuad(layer.transform, layer.scale, expand);
		const slug_t lidx = cv(index + 1);
		const slug_t z = cv(layer.transform.z);

		vertices->append_range({
			{q.x0, q.y0, z, lidx},
			{q.x1, q.y0, z, lidx},
			{q.x1, q.y1, z, lidx},
			{q.x0, q.y1, z, lidx}
		});

		colors->append_n<4>({layer.color.r, layer.color.g, layer.color.b, layer.color.a});

		// em-coords are the quad corners in local em-space (no scale, no origin). The fragment
		// shader uses these with the band transform to compute coverage.
		const auto emX0 = shape->bearingX - expand;
		const auto emY0 = (shape->bearingY - shape->height) - expand;
		const auto emX1 = (shape->bearingX + shape->width) + expand;
		const auto emY1 = shape->bearingY + expand;

		emCoords->append_range({
			{emX0, emY0, 0_cv, 0_cv},
			{emX1, emY0, 1_cv, 0_cv},
			{emX1, emY1, 1_cv, 1_cv},
			{emX0, emY1, 0_cv, 1_cv}
		});

		bandXform->append_n<4>({
			shape->bandScaleX,
			shape->bandScaleY,
			shape->bandOffsetX,
			shape->bandOffsetY
		});

		shapeData->append_n<4>({
			cv(shape->bandTexX),
			cv(shape->bandTexY),
			cv(shape->bandMaxX),
			cv(shape->bandMaxY)
		});

		effectData->append_n<4>(osg::Vec4(cv(layer.effectId), cv(shape->originX), cv(shape->originY), 0_cv));

		const auto [gmeta, gxform] = buildGradientData(*_atlas, layer);

		gradientMeta->append_n<4>(gmeta);
		gradientXforms->append_n<4>(gxform);

		indices->append_range({
			base, index_element_type(base + 1), index_element_type(base + 2),
			base, index_element_type(base + 2), index_element_type(base + 3)
		});

		base += 4;

		index++;
	}

	// TODO: Why JUST test `vertices`? Shouldn't we test them ALL!?
	if(vertices->empty()) return;

	setVertexAttribArray(0, vertices);
	setVertexAttribBinding(0, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(1, colors);
	setVertexAttribBinding(1, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(2, emCoords);
	setVertexAttribBinding(2, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(3, bandXform);
	setVertexAttribBinding(3, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(4, shapeData);
	setVertexAttribBinding(4, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(5, effectData);
	setVertexAttribBinding(5, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(6, gradientMeta);
	setVertexAttribBinding(6, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(7, gradientXforms);
	setVertexAttribBinding(7, osg::Geometry::BIND_PER_VERTEX);

	addPrimitiveSet(indices);
}

void GL3ShapeDrawable::setLayerColor(size_t index, const slughorn::Color& color) {
	if(index >= _layers.size()) return;

	auto* arr = static_cast<osgx::Vec4Array*>(getVertexAttribArray(1));

	if(!arr || (index + 1) * 4 > arr->size()) return;

	_layers[index].color = color;

	const Vec4 c(color.r, color.g, color.b, color.a);

	for(size_t v = 0; v < 4; v++) (*arr)[index * 4 + v] = c;
}

void GL3ShapeDrawable::setLayerEffectId(size_t index, uint32_t effectId) {
	if(index >= _layers.size()) return;

	auto* arr = static_cast<osgx::Vec4Array*>(getVertexAttribArray(5));

	if(!arr || (index + 1) * 4 > arr->size()) return;

	_layers[index].effectId = effectId;

	for(size_t v = 0; v < 4; v++) (*arr)[index * 4 + v].x() = cv(effectId);
}

void GL3ShapeDrawable::setLayerEffectParam(size_t index, slug_t param) {
	auto* arr = static_cast<osgx::Vec4Array*>(getVertexAttribArray(5));

	if(!arr || (index + 1) * 4 > arr->size()) return;

	for(size_t v = 0; v < 4; v++) (*arr)[index * 4 + v].w() = cv(param);
}

void GL3ShapeDrawable::updateLayer(size_t index, const slughorn::Layer& layer) {
	if(index >= _layers.size() || !_atlas) return;

	auto* colors = static_cast<osgx::Vec4Array*>(getVertexAttribArray(1));
	auto* effectData = static_cast<osgx::Vec4Array*>(getVertexAttribArray(5));
	auto* gradMeta = static_cast<osgx::Vec4Array*>(getVertexAttribArray(6));
	auto* gradXforms = static_cast<osgx::Vec4Array*>(getVertexAttribArray(7));

	// Can't these just be COMBINED? Who did this? :)
	if(!colors || !effectData || !gradMeta || !gradXforms) return;
	if((index + 1) * 4 > colors->size()) return;

	_layers[index] = layer;

	const auto* shape = _atlas->getShape(layer.key);
	const auto [gmeta, gxform] = buildGradientData(*_atlas, layer);

	const Vec4 c(layer.color.r, layer.color.g, layer.color.b, layer.color.a);
	const Vec4 eid(cv(layer.effectId), shape ? cv(shape->originX) : 0_cv, shape ? cv(shape->originY) : 0_cv, 0_cv);

	for(size_t v = 0; v < 4; v++) {
		(*colors)[index * 4 + v] = c;
		(*effectData)[index * 4 + v] = eid;
		(*gradMeta)[index * 4 + v] = gmeta;
		(*gradXforms)[index * 4 + v] = gxform;
	}
}

void GL3ShapeDrawable::dirtyLayers() {
	for(unsigned slot : {1u, 5u, 6u, 7u}) {
		if(auto* arr = getVertexAttribArray(slot)) arr->dirty();
	}
}

void BoxDrawable::compile() {
	auto* atlas = getAtlas();

	if(!atlas || !atlas->isBuilt() || _layers.empty()) return;

	auto vertices = osgx::make_ref<osgx::Vec4Array>();
	auto colors = osgx::make_ref<osgx::Vec4Array>();
	auto emCoords = osgx::make_ref<osgx::Vec4Array>();
	auto bandXform = osgx::make_ref<osgx::Vec4Array>();
	auto shapeData = osgx::make_ref<osgx::Vec4Array>();
	auto effectData = osgx::make_ref<osgx::Vec4Array>();
	auto gradientMeta = osgx::make_ref<osgx::Vec4Array>();
	auto gradientXforms = osgx::make_ref<osgx::Vec4Array>();
	// auto indices = osgx::make_ref<osgx::DrawElementsUShort>(osg::PrimitiveSet::TRIANGLES);
	auto indices = osgx::make_ref<osgx::DrawElementsUShort>();

	auto addFace = [&](Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, size_t index) {
		const auto& layer = _layers[index];
		const auto* shape = atlas->getShape(layer.key);

		if(!shape) return;

		static constexpr slug_t SLUG_EXPAND = 0.01_cv;

		const slug_t emX0 = shape->bearingX - SLUG_EXPAND;
		const slug_t emY0 = (shape->bearingY - shape->height) - SLUG_EXPAND;
		const slug_t emX1 = (shape->bearingX + shape->width) + SLUG_EXPAND;
		const slug_t emY1 = shape->bearingY + SLUG_EXPAND;

		const Vec4 bx(shape->bandScaleX, shape->bandScaleY, shape->bandOffsetX, shape->bandOffsetY);

		const Vec4 sd(
			cv(shape->bandTexX),
			cv(shape->bandTexY),
			cv(shape->bandMaxX),
			cv(shape->bandMaxY)
		);

		const Vec4 color(layer.color.r, layer.color.g, layer.color.b, layer.color.a);
		const osg::Vec4 eid(cv(layer.effectId), cv(shape->originX), cv(shape->originY), 0_cv);
		const slug_t lidx = cv(index + 1);

		const auto [gmeta, gxform] = buildGradientData(*_atlas, layer);

		auto base = static_cast<index_element_type>(vertices->size());

		Vec3 ps[4] = {p0, p1, p2, p3};

		slug_t uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

		for(size_t i = 0; i < 4; i++) {
			vertices->push_back(osg::Vec4(ps[i].x(), ps[i].y(), ps[i].z(), lidx));

			slug_t emX = emX0 + uvs[i][0] * (emX1 - emX0);
			slug_t emY = emY0 + uvs[i][1] * (emY1 - emY0);

			emCoords->push_back({emX, emY, uvs[i][0], uvs[i][1]});
			colors->push_back(color);
			bandXform->push_back(bx);
			shapeData->push_back(sd);
			effectData->push_back(eid);
			gradientMeta->push_back(gmeta);
			gradientXforms->push_back(gxform);
		}

		indices->append_range({
			base, index_element_type(base + 1), index_element_type(base + 2),
			base, index_element_type(base + 2), index_element_type(base + 3)
		});
	};

	auto s = _size * 0.5_cv;

	addFace({-s,-s, s}, { s,-s, s}, { s, s, s}, {-s, s, s}, 0); // +Z front
	addFace({ s,-s,-s}, {-s,-s,-s}, {-s, s,-s}, { s, s,-s}, 1); // -Z back
	addFace({ s,-s, s}, { s,-s,-s}, { s, s,-s}, { s, s, s}, 2); // +X right
	addFace({-s,-s,-s}, {-s,-s, s}, {-s, s, s}, {-s, s,-s}, 3); // -X left
	addFace({-s, s, s}, { s, s, s}, { s, s,-s}, {-s, s,-s}, 4); // +Y top
	addFace({-s,-s,-s}, { s,-s,-s}, { s,-s, s}, {-s,-s, s}, 5); // -Y bottom

	// Bind arrays (same slots as before)
	setVertexAttribArray(0, vertices);
	setVertexAttribBinding(0, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(1, colors);
	setVertexAttribBinding(1, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(2, emCoords);
	setVertexAttribBinding(2, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(3, bandXform);
	setVertexAttribBinding(3, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(4, shapeData);
	setVertexAttribBinding(4, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(5, effectData);
	setVertexAttribBinding(5, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(6, gradientMeta);
	setVertexAttribBinding(6, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(7, gradientXforms);
	setVertexAttribBinding(7, osg::Geometry::BIND_PER_VERTEX);

	addPrimitiveSet(indices);
}

void GL3SubdividedDrawable::compile() {
	auto* atlas = getAtlas();

	if(!atlas || !atlas->isBuilt() || _layers.empty()) return;

	auto vertices = osgx::make_ref<osgx::Vec4Array>();
	auto colors = osgx::make_ref<osgx::Vec4Array>();
	auto emCoords = osgx::make_ref<osgx::Vec4Array>();
	auto bandXform = osgx::make_ref<osgx::Vec4Array>();
	auto shapeData = osgx::make_ref<osgx::Vec4Array>();
	auto effectData = osgx::make_ref<osgx::Vec4Array>();
	auto gradientMeta = osgx::make_ref<osgx::Vec4Array>();
	auto gradientXforms = osgx::make_ref<osgx::Vec4Array>();
	auto indices = osgx::make_ref<osgx::DrawElementsUShort>();

	static constexpr slug_t SLUG_EXPAND = 0.01_cv;

	index_element_type base = 0;

	size_t index = 0;

	for(const auto& layer : _layers) {
		const auto* shape = atlas->getShape(layer.key);

		if(!shape) { index++; continue; }

		const auto q = shape->computeQuad(layer.transform, layer.scale, SLUG_EXPAND);

		// If no position callback is set, default to a flat quad covering the world-space
		// bounds computed above. This makes SubdividedDrawable usable as a drop-in
		// replacement for ShapeDrawable when denser geometry is needed (e.g. 9-slice
		// animation), without requiring a custom callback for every layer.
		PositionCallback posFn = _positionCallback
			? _positionCallback
			: PositionCallback([q](slug_t u, slug_t v) -> Vec3 {
				return {q.x0 + u * (q.x1 - q.x0), q.y0 + v * (q.y1 - q.y0), 0_cv};
			})
		;

		const slug_t emX0 = shape->bearingX - SLUG_EXPAND;
		const slug_t emY0 = (shape->bearingY - shape->height) - SLUG_EXPAND;
		const slug_t emX1 = (shape->bearingX + shape->width) + SLUG_EXPAND;
		const slug_t emY1 = shape->bearingY + SLUG_EXPAND;

		const Vec4 bx(
			shape->bandScaleX, shape->bandScaleY,
			shape->bandOffsetX, shape->bandOffsetY
		);

		const Vec4 sd(
			cv(shape->bandTexX), cv(shape->bandTexY),
			cv(shape->bandMaxX), cv(shape->bandMaxY)
		);

		const Vec4 color(layer.color.r, layer.color.g, layer.color.b, layer.color.a);

		// Pack the world-space width of this layer's quad into .w so the vertex shader
		// can recover the left edge (leftX = pos.x - uv.x * W) and implement effects
		// like 9-slice scaling without any additional uniforms.
		const osg::Vec4 eid(cv(layer.effectId), cv(shape->originX), cv(shape->originY), q.x1 - q.x0);

		const slug_t lidx = cv(index + 1);
		const auto [gmeta, gxform] = buildGradientData(*_atlas, layer);

		// Guard against the running total exceeding the uint16 index range.
		const size_t ni =
			static_cast<size_t>(_stepsU + 1) * static_cast<size_t>(_stepsV + 1)
		;

		if(
			static_cast<size_t>(base) + ni >
			static_cast<size_t>(std::numeric_limits<index_element_type>::max()) + 1
		) {
			throw std::runtime_error("SubdividedDrawable: mesh exceeds index capacity");
		}

		// Vertex grid: (_stepsV + 1) rows x (_stepsU + 1) cols.
		// em-coords are computed purely from (u,v), independent of the position callback.
		// This means the position callback can distort world-space geometry freely (cylinder,
		// sphere, 9-slice remap) without affecting the em-coord/Slug coverage mapping.
		for(index_element_type sv = 0; sv <= _stepsV; sv++) {
			const slug_t v = cv(sv) / cv(_stepsV);

			for(index_element_type su = 0; su <= _stepsU; su++) {
				const slug_t u = cv(su) / cv(_stepsU);

				const auto p = posFn(u, v);

				vertices->push_back(osg::Vec4(p.x(), p.y(), p.z(), lidx));

				emCoords->push_back({
					emX0 + u * (emX1 - emX0),
					emY0 + v * (emY1 - emY0),
					u,
					v
				});

				colors->push_back(color);
				bandXform->push_back(bx);
				shapeData->push_back(sd);
				effectData->push_back(eid);
				gradientMeta->push_back(gmeta);
				gradientXforms->push_back(gxform);
			}
		}

		// Index stitching - offset by layerBase so all layers share one index buffer.
		const index_element_type layerBase = base;

		for(index_element_type sv = 0; sv < _stepsV; sv++) {
			for(index_element_type su = 0; su < _stepsU; su++) {
				const auto row0 = static_cast<index_element_type>(layerBase + sv * (_stepsU + 1));
				const auto row1 = static_cast<index_element_type>(layerBase + (sv + 1) * (_stepsU + 1));
				const auto bl = static_cast<index_element_type>(row0 + su);
				const auto br = static_cast<index_element_type>(row0 + su + 1);
				const auto tl = static_cast<index_element_type>(row1 + su);
				const auto tr = static_cast<index_element_type>(row1 + su + 1);

				// TODO: Both approaches have polar artifacts; why!?
				// indices->append_range({bl, br, tl, br, tr, tl});

				if(!((su + sv) & 1)) indices->append_range({bl, br, tl, br, tr, tl});

				else indices->append_range({bl, br, tr, bl, tr, tl});
			}
		}

		base += static_cast<index_element_type>(ni);

		index++;
	}

	if(vertices->empty()) return;

	setVertexAttribArray(0, vertices);
	setVertexAttribBinding(0, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(1, colors);
	setVertexAttribBinding(1, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(2, emCoords);
	setVertexAttribBinding(2, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(3, bandXform);
	setVertexAttribBinding(3, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(4, shapeData);
	setVertexAttribBinding(4, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(5, effectData);
	setVertexAttribBinding(5, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(6, gradientMeta);
	setVertexAttribBinding(6, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(7, gradientXforms);
	setVertexAttribBinding(7, osg::Geometry::BIND_PER_VERTEX);

	addPrimitiveSet(indices);
}

void GL3SubdividedDrawable::setLayerEffectParam(size_t index, slug_t param) {
	auto* arr = static_cast<osgx::Vec4Array*>(getVertexAttribArray(5));

	if(!arr) return;

	const size_t stride =
		static_cast<size_t>(_stepsU + 1) * static_cast<size_t>(_stepsV + 1)
	;
	const size_t start = index * stride;

	if(start + stride > arr->size()) return;

	for(size_t v = 0; v < stride; v++) (*arr)[start + v].w() = cv(param);
}

void SSBOShapeDrawable::compile() {
	if(!_atlas || !_atlas->isBuilt() || _layers.empty()) return;

	auto vertices = osgx::make_ref<osgx::Vec4Array>();
	auto emCoords = osgx::make_ref<osgx::Vec4Array>();
	auto indices = osgx::make_ref<osgx::DrawElementsUShort>();

	// Layer SSBO (binding 1): one Vec4Array per layer, each holding 4 vec4s.
	// All arrays share a single ShaderStorageBufferObject so the GPU sees one contiguous buffer,
	// but each array has its own modifiedCount, enabling per-layer dirty uploads.
	//
	// [0] color: RGBA
	// [1] gradientMeta: x=gradientId, yz=center, w=r0_norm
	// [2] gradientXform
	// [3] effectData: x=effectId, y=shapeIndex, z=0, w=effectParam (0 for ShapeDrawable)
	_layerBuffers.clear();

	auto ssbo = osgx::make_ref<osg::ShaderStorageBufferObject>();

	index_element_type base = 0;
	size_t index = 0;

	for(const auto& layer : _layers) {
		const auto* shape = _atlas->getShape(layer.key);

		if(!shape) { index++; continue; }

		const slug_t expand = 0.01_cv;
		const auto q = shape->computeQuad(layer.transform, layer.scale, expand);
		const slug_t lidx = cv(index + 1);
		const slug_t z = cv(layer.transform.z);

		vertices->append_range({
			{q.x0, q.y0, z, lidx},
			{q.x1, q.y0, z, lidx},
			{q.x1, q.y1, z, lidx},
			{q.x0, q.y1, z, lidx}
		});

		const auto emX0 = shape->bearingX - expand;
		const auto emY0 = (shape->bearingY - shape->height) - expand;
		const auto emX1 = (shape->bearingX + shape->width) + expand;
		const auto emY1 = shape->bearingY + expand;

		emCoords->append_range({
			{emX0, emY0, 0_cv, 0_cv},
			{emX1, emY0, 1_cv, 0_cv},
			{emX1, emY1, 1_cv, 1_cv},
			{emX0, emY1, 0_cv, 1_cv}
		});

		const auto [gmeta, gxform] = buildGradientData(*_atlas, layer);
		const slug_t shapeIdx = cv(_atlas->getShapeIndex(layer.key));
		auto layerBuf = osgx::make_ref<osgx::Vec4Array>();

		layerBuf->push_back({layer.color.r, layer.color.g, layer.color.b, layer.color.a});
		layerBuf->push_back(gmeta);
		layerBuf->push_back(gxform);
		layerBuf->push_back({cv(layer.effectId), shapeIdx, 0_cv, 0_cv});
		layerBuf->setBufferObject(ssbo);

		_layerBuffers.push_back(std::move(layerBuf));

		indices->append_range({
			base, index_element_type(base + 1), index_element_type(base + 2),
			base, index_element_type(base + 2), index_element_type(base + 3)
		});

		base += 4;
		index++;
	}

	if(vertices->empty()) return;

	setVertexAttribArray(0, vertices);
	setVertexAttribBinding(0, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(1, emCoords);
	setVertexAttribBinding(1, osg::Geometry::BIND_PER_VERTEX);

	addPrimitiveSet(indices);

	const auto totalSize = static_cast<GLsizeiptr>(_layerBuffers.size() * 4 * sizeof(osg::Vec4));

	getOrCreateStateSet()->setAttributeAndModes(
		new osg::ShaderStorageBufferBinding(1, _layerBuffers[0], 0, totalSize),
		osg::StateAttribute::ON
	);
}

void SSBOShapeDrawable::setLayerColor(size_t index, const slughorn::Color& color) {
	if(index >= _layerBuffers.size()) return;

	ssboSetLayerColor(_layerBuffers[index].get(), _layers, index, color);
}

void SSBOShapeDrawable::setLayerEffectId(size_t index, uint32_t effectId) {
	if(index >= _layerBuffers.size()) return;

	ssboSetLayerEffectId(_layerBuffers[index].get(), _layers, index, effectId);
}

void SSBOShapeDrawable::setLayerEffectParam(size_t index, slug_t param) {
	if(index >= _layerBuffers.size()) return;

	ssboSetLayerEffectParam(_layerBuffers[index].get(), param);
}

void SSBOShapeDrawable::setLayerShapeIndex(size_t index, size_t shapeIndex) {
	if(index >= _layerBuffers.size()) return;

	(*_layerBuffers[index])[3].y() = cv(shapeIndex);
}

void SSBOShapeDrawable::updateLayer(size_t index, const slughorn::Layer& layer) {
	if(index >= _layerBuffers.size() || !_atlas) return;

	_layers[index] = layer;

	const auto [gmeta, gxform] = buildGradientData(*_atlas, layer);
	const slug_t shapeIdx = cv(_atlas->getShapeIndex(layer.key));

	auto& buf = *_layerBuffers[index];

	buf[0] = Vec4(layer.color.r, layer.color.g, layer.color.b, layer.color.a);
	buf[1] = gmeta;
	buf[2] = gxform;
	buf[3] = Vec4(cv(layer.effectId), shapeIdx, 0_cv, buf[3].w());
}

void SSBOShapeDrawable::dirtyLayers() {
	for(auto& buf : _layerBuffers) if(buf) buf->dirty();
}

void SSBOShapeDrawable::dirtyLayers(size_t index) {
	if(index < _layerBuffers.size() && _layerBuffers[index]) _layerBuffers[index]->dirty();
}

void SSBOSubdividedDrawable::compile() {
	auto* atlas = getAtlas();

	if(!atlas || !atlas->isBuilt() || _layers.empty()) return;

	auto vertices = osgx::make_ref<osgx::Vec4Array>();
	auto emCoords = osgx::make_ref<osgx::Vec4Array>();
	auto indices = osgx::make_ref<osgx::DrawElementsUShort>();

	_layerBuffers.clear();

	auto ssbo = osgx::make_ref<osg::ShaderStorageBufferObject>();

	static constexpr slug_t SLUG_EXPAND = 0.01_cv;

	index_element_type base = 0;
	size_t index = 0;

	for(const auto& layer : _layers) {
		const auto* shape = atlas->getShape(layer.key);

		if(!shape) { index++; continue; }

		const auto q = shape->computeQuad(layer.transform, layer.scale, SLUG_EXPAND);

		PositionCallback posFn = _positionCallback
			? _positionCallback
			: PositionCallback([q](slug_t u, slug_t v) -> Vec3 {
				return {q.x0 + u * (q.x1 - q.x0), q.y0 + v * (q.y1 - q.y0), 0_cv};
			})
		;

		const auto emX0 = shape->bearingX - SLUG_EXPAND;
		const auto emY0 = (shape->bearingY - shape->height) - SLUG_EXPAND;
		const auto emX1 = (shape->bearingX + shape->width) + SLUG_EXPAND;
		const auto emY1 = shape->bearingY + SLUG_EXPAND;

		const slug_t lidx = cv(index + 1);

		const size_t ni =
			static_cast<size_t>(_stepsU + 1) * static_cast<size_t>(_stepsV + 1)
		;

		if(
			static_cast<size_t>(base) + ni >
			static_cast<size_t>(std::numeric_limits<index_element_type>::max()) + 1
		) {
			throw std::runtime_error("SSBOSubdividedDrawable: mesh exceeds index capacity");
		}

		for(index_element_type sv = 0; sv <= _stepsV; sv++) {
			const slug_t v = cv(sv) / cv(_stepsV);

			for(index_element_type su = 0; su <= _stepsU; su++) {
				const slug_t u = cv(su) / cv(_stepsU);
				const auto p = posFn(u, v);

				vertices->push_back({p.x(), p.y(), p.z(), lidx});
				emCoords->push_back({
					emX0 + u * (emX1 - emX0),
					emY0 + v * (emY1 - emY0),
					u, v
				});
			}
		}

		const auto [gmeta, gxform] = buildGradientData(*atlas, layer);
		const slug_t shapeIdx = cv(atlas->getShapeIndex(layer.key));
		auto layerBuf = osgx::make_ref<osgx::Vec4Array>();

		layerBuf->push_back({layer.color.r, layer.color.g, layer.color.b, layer.color.a});
		layerBuf->push_back(gmeta);
		layerBuf->push_back(gxform);
		layerBuf->push_back({cv(layer.effectId), shapeIdx, 0_cv, q.x1 - q.x0});
		layerBuf->setBufferObject(ssbo);

		_layerBuffers.push_back(std::move(layerBuf));

		const index_element_type layerBase = base;

		for(index_element_type sv = 0; sv < _stepsV; sv++) {
			for(index_element_type su = 0; su < _stepsU; su++) {
				const auto row0 = static_cast<index_element_type>(layerBase + sv * (_stepsU + 1));
				const auto row1 = static_cast<index_element_type>(layerBase + (sv + 1) * (_stepsU + 1));
				const auto bl = static_cast<index_element_type>(row0 + su);
				const auto br = static_cast<index_element_type>(row0 + su + 1);
				const auto tl = static_cast<index_element_type>(row1 + su);
				const auto tr = static_cast<index_element_type>(row1 + su + 1);

				if(!((su + sv) & 1)) indices->append_range({bl, br, tl, br, tr, tl});

				else indices->append_range({bl, br, tr, bl, tr, tl});
			}
		}

		base += static_cast<index_element_type>(ni);
		index++;
	}

	if(vertices->empty()) return;

	setVertexAttribArray(0, vertices);
	setVertexAttribBinding(0, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(1, emCoords);
	setVertexAttribBinding(1, osg::Geometry::BIND_PER_VERTEX);

	addPrimitiveSet(indices);

	const auto totalSize = static_cast<GLsizeiptr>(_layerBuffers.size() * 4 * sizeof(osg::Vec4));

	getOrCreateStateSet()->setAttributeAndModes(
		new osg::ShaderStorageBufferBinding(1, _layerBuffers[0], 0, totalSize),
		osg::StateAttribute::ON
	);
}

void SSBOSubdividedDrawable::setLayerColor(size_t index, const slughorn::Color& color) {
	if(index >= _layerBuffers.size()) return;

	ssboSetLayerColor(_layerBuffers[index].get(), _layers, index, color);
}

void SSBOSubdividedDrawable::setLayerEffectId(size_t index, uint32_t effectId) {
	if(index >= _layerBuffers.size()) return;

	ssboSetLayerEffectId(_layerBuffers[index].get(), _layers, index, effectId);
}

void SSBOSubdividedDrawable::setLayerEffectParam(size_t index, slug_t param) {
	if(index >= _layerBuffers.size()) return;

	ssboSetLayerEffectParam(_layerBuffers[index].get(), param);
}

void SSBOSubdividedDrawable::updateLayer(size_t index, const slughorn::Layer& layer) {
	if(index >= _layerBuffers.size() || !_atlas) return;

	_layers[index] = layer;

	const auto [gmeta, gxform] = buildGradientData(*_atlas, layer);
	const slug_t shapeIdx = cv(_atlas->getShapeIndex(layer.key));

	auto& buf = *_layerBuffers[index];

	buf[0] = Vec4(layer.color.r, layer.color.g, layer.color.b, layer.color.a);
	buf[1] = gmeta;
	buf[2] = gxform;
	buf[3] = Vec4(cv(layer.effectId), shapeIdx, 0_cv, buf[3].w());
}

void SSBOSubdividedDrawable::dirtyLayers() {
	for(auto& buf : _layerBuffers) if(buf) buf->dirty();
}

void SSBOSubdividedDrawable::dirtyLayers(size_t index) {
	if(index < _layerBuffers.size() && _layerBuffers[index]) _layerBuffers[index]->dirty();
}

}
