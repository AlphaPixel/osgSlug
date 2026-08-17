#include "osgSlug/Drawable/SubdividedDrawable.hpp"

#include "Drawable/Util.hpp"

#include <osg/BufferIndexBinding>
#include <osg/BufferObject>
#include <algorithm>
#include <stdexcept>
#include <limits>

namespace osgSlug {

namespace {

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

}

void SubdividedDrawable::compile() {
	if(_compiled) return;

	auto* atlas = getAtlas();

	if(!atlas || !atlas->isBuilt() || _layers.empty()) return;

	// A custom _positionCallback (any surface other than the default flat plane) means the
	// single per-layer axisX/axisY frame (below) can't be exact everywhere - the true tangent
	// direction/rate varies with (u,v). In that case we compute a REAL per-vertex tangent frame
	// instead and switch to a program that reads it from per-vertex attributes rather than the
	// per-layer SSBO. See project_subdivided_curved_axes_bug (memory) for the bug this fixes.
	const bool curved = static_cast<bool>(_positionCallback);

	if(curved) getOrCreateStateSet()->setAttributeAndModes(
		atlas->createSubdividedProgram(),
		osg::StateAttribute::ON
	);

	const slug_t epsU = 0.5_cv / cv(_stepsU);
	const slug_t epsV = 0.5_cv / cv(_stepsV);

	auto vertices = osgx::make_ref<osgx::Vec4Array>();
	auto emCoords = osgx::make_ref<osgx::Vec4Array>();
	auto axisXArr = osgx::make_ref<osgx::Vec4Array>();
	auto axisYArr = osgx::make_ref<osgx::Vec4Array>();

	_groups.clear();

	osg::ref_ptr<index_type> groupIndices;

	slughorn::BlendMode groupBlend = slughorn::BlendMode::SrcOver;

	auto flushGroup = [&]() {
		if(groupIndices && !groupIndices->empty())
			_groups.push_back({groupBlend, slughorn::DrawMode::Visible, groupIndices, nullptr});
	};

	auto ssbo = osgx::make_ref<osg::ShaderStorageBufferObject>();

	index_element_type base = 0;

	for(size_t i = 0; i < _layers.size(); i++) {
		auto& rs = _layers[i];
		const auto& layer = rs.layer;
		const slug_t lidx = cv(i + 1);

		auto layerBuf = osgx::make_ref<osgx::Vec4Array>();
		const bool visible = layer.drawMode == slughorn::DrawMode::Visible;
		const auto shape = atlas->getShape(layer.key);

		if(visible && shape) {
			if(!groupIndices || layer.blendMode != groupBlend) {
				flushGroup();
				groupIndices = osgx::make_ref<index_type>();
				groupBlend = layer.blendMode;
			}

			// TRUE authored quad + em bounds (see ShapeDrawable::compile()) - the AA margin and
			// layer.bleed are pushed outward live in SHADER_VERT. Interior grid vertices get no
			// push (their uv is fractional, so osgSlug_CornerDir() returns 0 for them).
			const auto q = shape->computeQuad(layer.transform, layer.scale);

			PositionCallback posFn = _positionCallback
				? _positionCallback
				: PositionCallback([q](slug_t u, slug_t v) -> Vec3 {
					return {q.x0 + u * (q.x1 - q.x0), q.y0 + v * (q.y1 - q.y0), 0_cv};
				})
			;

			const auto [emX0, emY0, emX1, emY1] = computeEmBounds(*shape);

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

				vertices->push_back({p.x(), p.y(), p.z(), lidx});
				emCoords->push_back({emX0 + u * (emX1 - emX0), emY0 + v * (emY1 - emY0), u, v});

				if(curved) {
					// Real tangent of posFn at this vertex, one-sided at the [0,1] domain
					// boundary. Rate legitimately -> 0 at a genuine tangent-plane degeneracy
					// (e.g. a sphere's poles) - that correctly shrinks SHADER_VERT's AA margin
					// push toward zero there, which IS the fix for the pole-squeeze artifact.
					// The shader's own max(rate, 1e-8) clamp guards literal division by zero.
					const slug_t u0 = std::max(u - epsU, 0_cv), u1 = std::min(u + epsU, 1_cv);
					const slug_t v0 = std::max(v - epsV, 0_cv), v1 = std::min(v + epsV, 1_cv);

					Vec3 tangentU = (posFn(u1, v) - posFn(u0, v)) / cv(u1 - u0);
					Vec3 tangentV = (posFn(u, v1) - posFn(u, v0)) / cv(v1 - v0);

					const float lenU = tangentU.length();
					const float lenV = tangentV.length();
					const slug_t spanU = emX1 - emX0;
					const slug_t spanV = emY1 - emY0;
					const slug_t rateU = spanU > 0_cv ? cv(lenU) / spanU : 0_cv;
					const slug_t rateV = spanV > 0_cv ? cv(lenV) / spanV : 0_cv;

					if(lenU > 1e-6f) tangentU /= lenU;
					if(lenV > 1e-6f) tangentV /= lenV;

					axisXArr->push_back({cv(tangentU.x()), cv(tangentU.y()), cv(tangentU.z()), rateU});
					axisYArr->push_back({cv(tangentV.x()), cv(tangentV.y()), cv(tangentV.z()), rateV});
				}
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
			layerBuf->push_back({layer.transform.x, layer.transform.y, layer.bleed, 0_cv});
			// [5]/[6] quad frame for SHADER_VERT's live margin push (see ShapeDrawable::compile()).
			// Only correct/used for the default planar posFn - a curved custom _positionCallback
			// gets a REAL per-vertex tangent frame instead (axisXArr/axisYArr above, read via the
			// OSGSLUG_AXIS_PER_VERTEX program from createSubdividedProgram()), so these two
			// per-layer entries are dead weight in that case, kept only to hold the LayerData
			// struct's stride. See project_subdivided_curved_axes_bug (memory) for the bug this
			// replaced: a single global flat-XY frame was silently wrong for any curved posFn,
			// worst at high-curvature regions (a UV wrap seam, a sphere's poles).
			layerBuf->push_back({1_cv, 0_cv, 0_cv, layer.scale});
			layerBuf->push_back({0_cv, 1_cv, 0_cv, layer.scale});

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

	if(curved) {
		setVertexAttribArray(2, axisXArr);
		setVertexAttribBinding(2, osg::Geometry::BIND_PER_VERTEX);
		setVertexAttribArray(3, axisYArr);
		setVertexAttribBinding(3, osg::Geometry::BIND_PER_VERTEX);
	}

	for(const auto& g : _groups) addPrimitiveSet(g.indices);

	const auto totalSize = static_cast<GLsizeiptr>(_layers.size() * 7 * sizeof(Vec4));

	getOrCreateStateSet()->setAttributeAndModes(
		new osg::ShaderStorageBufferBinding(1, _layers[0].buffer, 0, totalSize),
		osg::StateAttribute::ON
	);

	_compiled = true;
}

}
