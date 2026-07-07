#include "osgSlug/Drawable/GL3SubdividedDrawable.hpp"

#include "Drawable/Util.hpp"

#include <stdexcept>
#include <limits>

namespace osgSlug {

void GL3SubdividedDrawable::compile() {
	if(_compiled) return;

	auto* atlas = getAtlas();

	if(!atlas) {
		OSG_WARN << "GL3SubdividedDrawable::compile(): no Atlas parent -- "
			<< "add to an Atlas node first (e.g. atlas->addChild(drawable))" << std::endl;
		return;
	}

	if(!atlas->isBuilt() || _layers.empty()) return;

	auto vertices = osgx::make_ref<osgx::Vec4Array>();
	auto colors = osgx::make_ref<osgx::Vec4Array>();
	auto emCoords = osgx::make_ref<osgx::Vec4Array>();
	auto bandXform = osgx::make_ref<osgx::Vec4Array>();
	auto shapeData = osgx::make_ref<osgx::Vec4Array>();
	auto effectData = osgx::make_ref<osgx::Vec4Array>();
	auto gradientMeta = osgx::make_ref<osgx::Vec4Array>();
	auto gradientXforms = osgx::make_ref<osgx::Vec4Array>();

	_groups.clear();

	osg::ref_ptr<index_type> groupIndices;
	slughorn::BlendMode groupBlend = slughorn::BlendMode::SrcOver;

	auto flushGroup = [&]() {
		if(groupIndices && !groupIndices->empty())
			_groups.push_back({groupBlend, slughorn::DrawMode::Visible, groupIndices, nullptr});
	};

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
		const Vec4 eid(cv(layer.effectId), cv(shape->originX), cv(shape->originY), q.x1 - q.x0);

		const slug_t lidx = cv(index + 1);
		const auto [gmeta, gxform] = buildGradientData(*atlas, layer);

		const size_t ni = _isolatedVertices
			? static_cast<size_t>(_stepsU) * static_cast<size_t>(_stepsV) * 4
			: static_cast<size_t>(_stepsU + 1) * static_cast<size_t>(_stepsV + 1)
		;

		if(
			static_cast<size_t>(base) + ni >
			static_cast<size_t>(std::numeric_limits<index_element_type>::max()) + 1
		) {
			throw std::runtime_error("SubdividedDrawable: mesh exceeds index capacity");
		}

		auto pushVertex = [&](slug_t u, slug_t v) {
			const auto p = posFn(u, v);

			vertices->push_back(Vec4(p.x(), p.y(), p.z(), lidx));
			emCoords->push_back({emX0 + u * (emX1 - emX0), emY0 + v * (emY1 - emY0), u, v});
			colors->push_back(color);
			bandXform->push_back(bx);
			shapeData->push_back(sd);
			effectData->push_back(eid);
			gradientMeta->push_back(gmeta);
			gradientXforms->push_back(gxform);
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

	bindGL3Attribs(vertices, colors, emCoords, bandXform, shapeData, effectData, gradientMeta, gradientXforms);

	for(const auto& g : _groups) addPrimitiveSet(g.indices);

	_compiled = true;
}

void GL3SubdividedDrawable::setLayerEffectParam(size_t index, slug_t param) {
	auto* arr = static_cast<osgx::Vec4Array*>(getVertexAttribArray(5));

	if(!arr) {
		OSG_WARN << "GL3SubdividedDrawable::setLayerEffectParam(): called before compile() -- call ignored" << std::endl;
		return;
	}

	const size_t stride =
		static_cast<size_t>(_stepsU + 1) * static_cast<size_t>(_stepsV + 1)
	;
	const size_t start = index * stride;

	if(start + stride > arr->size()) return;

	for(size_t v = 0; v < stride; v++) (*arr)[start + v].w() = cv(param);
}

}
