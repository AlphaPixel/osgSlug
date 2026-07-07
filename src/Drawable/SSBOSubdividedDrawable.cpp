#include "osgSlug/Drawable/SSBOSubdividedDrawable.hpp"

#include "Drawable/Util.hpp"

#include <osg/BufferIndexBinding>
#include <osg/BufferObject>
#include <stdexcept>
#include <limits>

namespace osgSlug {

void SSBOSubdividedDrawable::compile() {
	if(_compiled) return;

	auto* atlas = getAtlas();

	if(!atlas || !atlas->isBuilt() || _layers.empty()) return;

	auto vertices = osgx::make_ref<osgx::Vec4Array>();
	auto emCoords = osgx::make_ref<osgx::Vec4Array>();

	_groups.clear();
	_layerBuffers.clear();

	osg::ref_ptr<index_type> groupIndices;

	slughorn::BlendMode groupBlend = slughorn::BlendMode::SrcOver;

	auto flushGroup = [&]() {
		if(groupIndices && !groupIndices->empty())
			_groups.push_back({groupBlend, slughorn::DrawMode::Visible, groupIndices, nullptr});
	};

	auto ssbo = osgx::make_ref<osg::ShaderStorageBufferObject>();

	index_element_type base = 0;
	size_t index = 0;

	for(const auto& layer : _layers) {
		if(layer.drawMode != slughorn::DrawMode::Visible) continue;

		const auto shape = atlas->getShape(layer.key);

		if(!shape) { index++; continue; }

		if(!groupIndices || layer.blendMode != groupBlend) {
			flushGroup();
			groupIndices = osgx::make_ref<index_type>();
			groupBlend = layer.blendMode;
		}

		const slug_t expand = layer.expand;
		const auto q = shape->computeQuad(layer.transform, layer.scale, expand);

		PositionCallback posFn = _positionCallback
			? _positionCallback
			: PositionCallback([q](slug_t u, slug_t v) -> Vec3 {
				return {q.x0 + u * (q.x1 - q.x0), q.y0 + v * (q.y1 - q.y0), 0_cv};
			})
		;

		const auto [emX0, emY0, emX1, emY1] = computeEmBounds(*shape, expand);

		const slug_t lidx = cv(index + 1);

		const size_t ni = _isolatedVertices
			? static_cast<size_t>(_stepsU) * static_cast<size_t>(_stepsV) * 4
			: static_cast<size_t>(_stepsU + 1) * static_cast<size_t>(_stepsV + 1)
		;

		if(
			static_cast<size_t>(base) + ni >
			static_cast<size_t>(std::numeric_limits<index_element_type>::max()) + 1
		) {
			throw std::runtime_error("SSBOSubdividedDrawable: mesh exceeds index capacity");
		}

		auto pushVertex = [&](slug_t u, slug_t v) {
			const auto p = posFn(u, v);

			vertices->push_back({p.x(), p.y(), p.z(), lidx});
			emCoords->push_back({emX0 + u * (emX1 - emX0), emY0 + v * (emY1 - emY0), u, v});
		};

		if(_isolatedVertices) {
			for(index_element_type sv = 0; sv < _stepsV; sv++) {
				for(index_element_type su = 0; su < _stepsU; su++) {
					const slug_t u0 = cv(su) / cv(_stepsU);
					const slug_t u1 = cv(su + 1) / cv(_stepsU);
					const slug_t v0 = cv(sv) / cv(_stepsV);
					const slug_t v1 = cv(sv + 1) / cv(_stepsV);

					pushVertex(u0, v0); // BL
					pushVertex(u1, v0); // BR
					pushVertex(u0, v1); // TL
					pushVertex(u1, v1); // TR
				}
			}
		}
		else {
			for(index_element_type sv = 0; sv <= _stepsV; sv++) {
				const slug_t v = cv(sv) / cv(_stepsV);

				for(index_element_type su = 0; su <= _stepsU; su++) {
					const slug_t u = cv(su) / cv(_stepsU);

					pushVertex(u, v);
				}
			}
		}

		const auto [gmeta, gxform] = buildGradientData(*atlas, layer);
		const slug_t shapeIdx = cv(atlas->getShapeIndex(layer.key));
		auto layerBuf = osgx::make_ref<osgx::Vec4Array>();

		layerBuf->push_back({layer.color.r, layer.color.g, layer.color.b, layer.color.a});
		layerBuf->push_back(gmeta);
		layerBuf->push_back(gxform);
		layerBuf->push_back({
			cv(layer.effectId),
			shapeIdx,
			cv(packMSDFData(shape->msdfLayer, shape->msdfRange)),
			q.x1 - q.x0
		});
		// osgSlug_LayerData grew a 5th slot (transformData) for masking, which this class
		// doesn't support -- pack the correct value anyway (cheap, already in scope) purely to
		// keep this buffer's per-layer stride matching the shared struct's size.
		layerBuf->push_back({layer.transform.x, layer.transform.y, 0_cv, 0_cv});
		layerBuf->setBufferObject(ssbo);

		_layerBuffers.push_back(std::move(layerBuf));

		const index_element_type layerBase = base;

		if(_isolatedVertices) {
			for(index_element_type sv = 0; sv < _stepsV; sv++) {
				for(index_element_type su = 0; su < _stepsU; su++) {
					const auto cell = static_cast<index_element_type>(
						layerBase + (sv * _stepsU + su) * 4
					);
					const auto bl = static_cast<index_element_type>(cell + 0);
					const auto br = static_cast<index_element_type>(cell + 1);
					const auto tl = static_cast<index_element_type>(cell + 2);
					const auto tr = static_cast<index_element_type>(cell + 3);

					if(!((su + sv) & 1)) groupIndices->append_range({tl, br, bl, tl, tr, br});
					else groupIndices->append_range({tr, br, bl, tl, tr, bl});
				}
			}
		}
		else {
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
		}

		base += static_cast<index_element_type>(ni);
		index++;
	}

	flushGroup();

	if(vertices->empty()) return;

	bindSSBOAttribs(vertices, emCoords);

	for(const auto& g : _groups) addPrimitiveSet(g.indices);

	const auto totalSize = static_cast<GLsizeiptr>(_layerBuffers.size() * 5 * sizeof(Vec4));

	getOrCreateStateSet()->setAttributeAndModes(
		new osg::ShaderStorageBufferBinding(1, _layerBuffers[0], 0, totalSize),
		osg::StateAttribute::ON
	);

	_compiled = true;
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
	if(index >= _layerBuffers.size()) {
		if(!_compiled)
			OSG_WARN << "SSBOSubdividedDrawable::setLayerEffectParam(): called before compile() -- call ignored" << std::endl;
		return;
	}

	ssboSetLayerEffectParam(_layerBuffers[index].get(), _layers, index, param);
}

void SSBOSubdividedDrawable::updateLayer(size_t index, const slughorn::Layer& layer) {
	auto* atlas = getAtlas();

	if(index >= _layerBuffers.size() || !atlas) return;

	_layers[index] = layer;

	const auto [gmeta, gxform] = buildGradientData(*atlas, layer);
	const slug_t shapeIdx = cv(atlas->getShapeIndex(layer.key));

	auto& buf = *_layerBuffers[index];

	buf[0] = Vec4(layer.color.r, layer.color.g, layer.color.b, layer.color.a);
	buf[1] = gmeta;
	buf[2] = gxform;
	buf[3] = Vec4(cv(layer.effectId), shapeIdx, buf[3].z(), buf[3].w());
	buf[4] = Vec4(layer.transform.x, layer.transform.y, 0_cv, 0_cv);
}

void SSBOSubdividedDrawable::setLayerGradientTransform(size_t index, const slughorn::Matrix& m) {
	auto* atlas = getAtlas();

	if(index >= _layerBuffers.size() || !atlas) return;

	const auto& layer = _layers[index];

	if(layer.gradientId <= 0) return;

	slughorn::GradientInfo tmp = atlas->getGradients()[layer.gradientId - 1];

	tmp.transform = m;

	const auto [gmeta, gxform] = buildGradientDataFromInfo(layer.gradientId, tmp);
	auto& buf = *_layerBuffers[index];

	buf[1] = gmeta;
	buf[2] = gxform;
}

void SSBOSubdividedDrawable::dirtyLayers() {
	for(auto& buf : _layerBuffers) if(buf) buf->dirty();
}

void SSBOSubdividedDrawable::dirtyLayers(size_t index) {
	if(index < _layerBuffers.size() && _layerBuffers[index]) _layerBuffers[index]->dirty();
}

}
