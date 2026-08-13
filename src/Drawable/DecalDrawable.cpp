#include "osgSlug/Drawable/DecalDrawable.hpp"

#include "Drawable/Util.hpp"

#include <osg/BufferIndexBinding>
#include <osg/BufferObject>
#include <stdexcept>
#include <limits>
#include <cmath>

namespace osgSlug {

namespace {

void computeDecalTangentFrame(
	slug_t latDeg,
	slug_t lonDeg,
	slug_t halfWidthDeg,
	slug_t halfHeightDeg,
	slug_t radius,
	Vec4& center,
	Vec4& tangentEast,
	Vec4& tangentNorth
) {
	const slug_t lat = latDeg * M_PIf / 180.f;
	const slug_t lon = lonDeg * M_PIf / 180.f;
	const slug_t cx = std::cos(lat) * std::cos(lon);
	const slug_t cy = std::sin(lat);
	const slug_t cz = std::cos(lat) * std::sin(lon);

	// East tangent: d/dlon of (cos(lat)*cos(lon), sin(lat), cos(lat)*sin(lon)), normalized.
	const slug_t ex = -std::sin(lon);
	const slug_t ez = std::cos(lon);

	// North tangent: d/dlat of same, normalized.
	const slug_t nx = -std::sin(lat) * std::cos(lon);
	const slug_t ny = std::cos(lat);
	const slug_t nz = -std::sin(lat) * std::sin(lon);

	// Scale: lu ? [-0.5, 0.5] -> arc = radius x halfDeg_rad at edge
	// so fullScale = 2 x radius x halfDeg_rad
	const slug_t scaleW = 2.f * radius * halfWidthDeg * M_PIf / 180.f;
	const slug_t scaleH = 2.f * radius * halfHeightDeg * M_PIf / 180.f;

	center = Vec4(cx, cy, cz, radius);
	tangentEast = Vec4(ex * scaleW, 0.f, ez * scaleW, 0.f);
	tangentNorth = Vec4(nx * scaleH, ny * scaleH, nz * scaleH, 0.f);
}

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

void DecalDrawable::addDecal(
	const slughorn::Layer& layer,
	slug_t latDeg,
	slug_t lonDeg,
	slug_t halfWidthDeg,
	slug_t halfHeightDeg
) {
	Anchor anchor;

	anchor.latDeg = latDeg;
	anchor.lonDeg = lonDeg;
	anchor.halfWidthDeg = halfWidthDeg;
	anchor.halfHeightDeg = halfHeightDeg;

	_decalEntries.push_back({layer, anchor});
	addLayer(layer);
}

void DecalDrawable::addPlanarDecal(
	const slughorn::Layer& layer,
	const Vec3& origin,
	const Vec3& tangentU,
	const Vec3& tangentV
) {
	Anchor anchor;

	anchor.planar = true;
	anchor.origin = origin;
	anchor.tangentU = tangentU;
	anchor.tangentV = tangentV;

	_decalEntries.push_back({layer, anchor});
	addLayer(layer);
}

void DecalDrawable::updateDecalPosition(
	size_t index,
	slug_t latDeg,
	slug_t lonDeg,
	slug_t halfWidthDeg,
	slug_t halfHeightDeg
) {
	if(index >= _decalEntries.size() || index >= _layers.size() || !_layers[index].buffer) return;

	auto& entry = _decalEntries[index];

	entry.anchor.planar = false;
	entry.anchor.latDeg = latDeg;
	entry.anchor.lonDeg = lonDeg;
	entry.anchor.halfWidthDeg = halfWidthDeg;
	entry.anchor.halfHeightDeg = halfHeightDeg;

	const slug_t halfH = halfHeightDeg < 0.f ? halfWidthDeg : halfHeightDeg;

	Vec4 center, tangentEast, tangentNorth;

	computeDecalTangentFrame(
		latDeg, lonDeg, halfWidthDeg, halfH, cv(_radius),
		center, tangentEast, tangentNorth
	);

	auto& buf = *_layers[index].buffer;

	buf[4] = center;
	buf[5] = tangentEast;
	buf[6] = tangentNorth;

	dirtyLayers(index);
}

void DecalDrawable::updatePlanarDecalPosition(
	size_t index,
	const Vec3& origin,
	const Vec3& tangentU,
	const Vec3& tangentV
) {
	if(index >= _decalEntries.size() || index >= _layers.size() || !_layers[index].buffer) return;

	auto& entry = _decalEntries[index];

	entry.anchor.planar = true;
	entry.anchor.origin = origin;
	entry.anchor.tangentU = tangentU;
	entry.anchor.tangentV = tangentV;

	auto& buf = *_layers[index].buffer;

	// center.w = 0 is the planar sentinel the vertex shader branches on - see SHADER_VERT_DECAL.
	buf[4] = Vec4(origin, 0_cv);
	buf[5] = Vec4(tangentU, 0_cv);
	buf[6] = Vec4(tangentV, 0_cv);

	dirtyLayers(index);
}

void DecalDrawable::setDecalTransform(
	size_t index,
	slug_t latDeg,
	slug_t lonDeg,
	slug_t halfWidthDeg,
	slug_t halfHeightDeg,
	slug_t rotationAngle
) {
	if(index >= _decalEntries.size() || index >= _layers.size() || !_layers[index].buffer) return;

	auto& entry = _decalEntries[index];

	entry.anchor.planar = false;
	entry.anchor.latDeg = latDeg;
	entry.anchor.lonDeg = lonDeg;
	entry.anchor.halfWidthDeg = halfWidthDeg;
	entry.anchor.halfHeightDeg = halfHeightDeg;

	const slug_t halfH = halfHeightDeg < 0.f ? halfWidthDeg : halfHeightDeg;

	Vec4 center, Te, Tn;

	computeDecalTangentFrame(latDeg, lonDeg, halfWidthDeg, halfH, cv(_radius), center, Te, Tn);

	if(rotationAngle != 0.f) {
		const slug_t c = std::cos(rotationAngle);
		const slug_t s = std::sin(rotationAngle);
		const Vec3 te(Te.x(), Te.y(), Te.z());
		const Vec3 tn(Tn.x(), Tn.y(), Tn.z());

		Te = Vec4(c*te.x() + s*tn.x(), c*te.y() + s*tn.y(), c*te.z() + s*tn.z(), 0.f);
		Tn = Vec4(-s*te.x() + c*tn.x(), -s*te.y() + c*tn.y(), -s*te.z() + c*tn.z(), 0.f);
	}

	auto& buf = *_layers[index].buffer;

	buf[4] = center;
	buf[5] = Te;
	buf[6] = Tn;

	dirtyLayers(index);
}

osg::BoundingBox DecalDrawable::computeBoundingBox() const {
	osg::BoundingBox bb;
	bool hasSphere = false;

	for(const auto& entry : _decalEntries) {
		const auto& anchor = entry.anchor;

		if(anchor.planar) {
			const Vec3 halfU = anchor.tangentU * 0.5f;
			const Vec3 halfV = anchor.tangentV * 0.5f;

			for(float su : {-1.0f, 1.0f}) {
				for(float sv : {-1.0f, 1.0f}) {
					bb.expandBy(anchor.origin + halfU * su + halfV * sv);
				}
			}
		}

		else hasSphere = true;
	}

	// Every sphere-type decal in a DecalDrawable shares the same globe, centered at this
	// drawable's own local origin with radius _radius (see SHADER_VERT_DECAL) - one shared
	// bound covers every sphere decal regardless of its individual lat/lon.
	if(hasSphere) {
		const auto r = static_cast<float>(_radius);

		bb.expandBy(Vec3(-r, -r, -r));
		bb.expandBy(Vec3(r, r, r));
	}

	return bb;
}

void DecalDrawable::updateLayer(size_t index, const slughorn::Layer& layer) {
	if(index >= _decalEntries.size()) return;

	_decalEntries[index].layer = layer;

	SubdividedDrawable::updateLayer(index, layer);
}

void DecalDrawable::compile() {
	if(_compiled) return;

	auto* atlas = getAtlas();

	if(!atlas || !atlas->isBuilt() || _decalEntries.empty()) return;

	getOrCreateStateSet()->setAttributeAndModes(
		atlas->createDecalProgram(),
		osg::StateAttribute::ON
	);

	auto vertices = osgx::make_ref<osgx::Vec4Array>();
	auto emCoords = osgx::make_ref<osgx::Vec4Array>();

	_groups.clear();

	osg::ref_ptr<index_type> groupIndices;
	slughorn::BlendMode groupBlend = slughorn::BlendMode::SrcOver;

	auto flushGroup = [&]() {
		if(groupIndices && !groupIndices->empty())
			_groups.push_back({groupBlend, slughorn::DrawMode::Visible, groupIndices, nullptr});
	};

	auto ssbo = osgx::make_ref<osg::ShaderStorageBufferObject>();

	index_element_type base = 0;

	for(size_t i = 0; i < _decalEntries.size(); i++) {
		const auto& [layer, anchor] = _decalEntries[i];
		const slug_t lidx = cv(i + 1);

		auto layerBuf = osgx::make_ref<osgx::Vec4Array>();
		const auto shape = atlas->getShape(layer.key);

		if(shape) {
			if(!groupIndices || layer.blendMode != groupBlend) {
				flushGroup();
				groupIndices = osgx::make_ref<index_type>();
				groupBlend = layer.blendMode;
			}

			// TODO(expand-removal): DecalDrawable still bakes a fixed em-space AA margin locally
			// (its grid maps a padded em range across the fixed tangent-frame extent, insetting
			// content slightly - the pre-removal behavior). Adapt to the GPU-live margin (like
			// ShapeDrawable/SHADER_VERT) when decals get the full treatment; computeQuad()/
			// computeEmBounds() themselves now always return TRUE bounds.
			static constexpr slug_t DECAL_EXPAND = 0.01_cv;

			const auto q = shape->computeQuad(layer.transform, layer.scale);
			auto [emX0, emY0, emX1, emY1] = computeEmBounds(*shape);

			emX0 -= DECAL_EXPAND;
			emY0 -= DECAL_EXPAND;
			emX1 += DECAL_EXPAND;
			emY1 += DECAL_EXPAND;

			const size_t ni = static_cast<size_t>(_stepsU + 1) * static_cast<size_t>(_stepsV + 1);

			if(
				static_cast<size_t>(base) + ni >
				static_cast<size_t>(std::numeric_limits<index_element_type>::max()) + 1
			) {
				throw std::runtime_error("DecalDrawable: mesh exceeds index capacity");
			}

			// Vertex data: normalized [0,1]^2 grid. World position is computed in the vertex shader
			// from the tangent frame in the SSBO - no sphere math on the CPU.
			for(index_element_type sv = 0; sv <= _stepsV; sv++) {
				const slug_t lv = cv(sv) / cv(_stepsV);

				for(index_element_type su = 0; su <= _stepsU; su++) {
					const slug_t lu = cv(su) / cv(_stepsU);

					vertices->push_back({lu, lv, 0_cv, lidx});
					emCoords->push_back({
						emX0 + lu * (emX1 - emX0),
						emY0 + lv * (emY1 - emY0),
						lu, lv
					});
				}
			}

			// 7-Vec4 SSBO: [0..3] standard layer data, [4..6] tangent frame.
			const auto [gmeta, gxform] = buildGradientData(*atlas, layer);
			const slug_t shapeIdx = cv(atlas->getShapeIndex(layer.key));

			layerBuf->push_back({layer.color.r, layer.color.g, layer.color.b, layer.color.a}); // [0]
			layerBuf->push_back(gmeta); // [1]
			layerBuf->push_back(gxform); // [2]
			layerBuf->push_back({
				cv(layer.effectId),
				shapeIdx,
				cv(packMSDFData(shape->msdfLayer, shape->msdfRange)),
				// Padded width (pre-removal parity): q is now the TRUE quad, so re-add the local
				// margin this drawable still bakes (see DECAL_EXPAND above).
				(q.x1 - q.x0) + 2_cv * DECAL_EXPAND * layer.scale
			}); // [3]

			Vec4 center, tangentEast, tangentNorth;

			if(anchor.planar) {
				// center.w = 0 is the planar sentinel the vertex shader branches on - no sphere
				// radius, no reprojection, see SHADER_VERT_DECAL.
				center = Vec4(anchor.origin, 0_cv);
				tangentEast = Vec4(anchor.tangentU, 0_cv);
				tangentNorth = Vec4(anchor.tangentV, 0_cv);
			}

			else {
				const slug_t halfH = anchor.halfHeightDeg < 0.f ? anchor.halfWidthDeg : anchor.halfHeightDeg;

				computeDecalTangentFrame(
					anchor.latDeg, anchor.lonDeg, anchor.halfWidthDeg, halfH, cv(_radius),
					center, tangentEast, tangentNorth
				);
			}

			layerBuf->push_back(center); // [4]
			layerBuf->push_back(tangentEast); // [5]
			layerBuf->push_back(tangentNorth); // [6]

			const index_element_type layerBase = base;

			for(index_element_type sv = 0; sv < _stepsV; sv++) {
				for(index_element_type su = 0; su < _stepsU; su++) {
					const auto row0 = static_cast<index_element_type>(layerBase + sv * (_stepsU + 1));
					const auto row1 = static_cast<index_element_type>(layerBase + (sv + 1) * (_stepsU + 1));
					const auto bl = static_cast<index_element_type>(row0 + su);
					const auto br = static_cast<index_element_type>(row0 + su + 1);
					const auto tl = static_cast<index_element_type>(row1 + su);
					const auto tr = static_cast<index_element_type>(row1 + su + 1);

					if(!((su + sv) & 1)) groupIndices->append_range({tl, br, bl, tl, tr, br});
					else groupIndices->append_range({tr, br, bl, tl, tr, bl});
				}
			}

			base += static_cast<index_element_type>(ni);
		}
		else {
			pushEmptySlot(*layerBuf);
		}

		layerBuf->setBufferObject(ssbo);

		if(i < _layers.size()) _layers[i].buffer = layerBuf;
	}

	flushGroup();

	if(vertices->empty()) return;

	bindSSBOAttribs(vertices, emCoords);

	for(const auto& g : _groups) addPrimitiveSet(g.indices);

	const auto totalSize = static_cast<GLsizeiptr>(_decalEntries.size() * 7 * sizeof(Vec4));

	getOrCreateStateSet()->setAttributeAndModes(
		new osg::ShaderStorageBufferBinding(1, _layers[0].buffer, 0, totalSize),
		osg::StateAttribute::ON
	);

	_compiled = true;
}

}
