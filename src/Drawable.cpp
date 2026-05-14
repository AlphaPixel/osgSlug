#include "osgSlug/Drawable.hpp"

namespace osgSlug {

ShapeDrawable::ShapeDrawable() {
	setUseDisplayList(false);
	setUseVertexBufferObjects(true);
}

void ShapeDrawable::compile() {
	if(!_atlas || !_atlas->isBuilt() || _layers.empty()) return;

	auto vertices = osgx::make_ref<osgx::Vec3Array>();
	auto colors = osgx::make_ref<osgx::Vec4Array>();
	auto emCoords = osgx::make_ref<osgx::Vec2Array>();
	auto bandXform = osgx::make_ref<osgx::Vec4Array>();
	auto shapeData = osgx::make_ref<osgx::Vec4Array>();
	auto effectIds = osgx::make_ref<osgx::FloatArray>();
	auto gradientIds = osgx::make_ref<osgx::FloatArray>();
	auto gradientXforms = osgx::make_ref<osgx::Vec4Array>();
	auto indices = osgx::make_ref<osgx::DrawElementsUShort>();

	// TODO (known): clear arrays here to avoid accumulation on repeated compile() calls. Safe for
	// now since compile() is only called once.

	index_element_type base = 0;

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

		vertices->append_range({
			{q.x0, q.y0, 0_cv},
			{q.x1, q.y0, 0_cv},
			{q.x1, q.y1, 0_cv},
			{q.x0, q.y1, 0_cv}
		});

		colors->append_n<4>({layer.color.r, layer.color.g, layer.color.b, layer.color.a});

		// em-coords are the quad corners in local em-space (no scale, no origin). The fragment
		// shader uses these with the band transform to compute coverage.
		const auto emX0 = shape->bearingX - expand;
		const auto emY0 = (shape->bearingY - shape->height) - expand;
		const auto emX1 = (shape->bearingX + shape->width) + expand;
		const auto emY1 = shape->bearingY + expand;

		emCoords->append_range({
			{emX0, emY0},
			{emX1, emY0},
			{emX1, emY1},
			{emX0, emY1}
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

		effectIds->append_n<4>(cv(layer.effectId));

		gradientIds->append_n<4>(cv(layer.gradientId));

		osg::Vec4 gx(0.0f, 0.0f, 0.0f, 0.0f);

		if(layer.gradientId > 0) {
			const auto& grad = _atlas->getGradients()[layer.gradientId - 1];
			const auto& m = grad.transform;

			if(grad.type == slughorn::GradientInfo::Type::Radial) {
				const float deltaR = m.xx - grad.innerRadius;
				const float invDeltaR = deltaR > 1e-6f ? 1.0f / deltaR : 0.0f;

				gx = { m.dx, m.dy, grad.innerRadius * invDeltaR, invDeltaR };
			}

			else if(grad.type == slughorn::GradientInfo::Type::Sweep) {
				const float arcSpan = m.xy;
				const float invArcSpan = arcSpan > 1e-6f ? 1.0f / arcSpan : 0.0f;

				gx = { m.dx, m.dy, m.xx, -invArcSpan }; // w < 0 = sweep
			}

			else gx = { m.xx, m.xy, m.dx, 0.0f };
		}

		gradientXforms->append_n<4>(gx);

		indices->append_range({
			base, index_element_type(base + 1), index_element_type(base + 2),
			base, index_element_type(base + 2), index_element_type(base + 3)
		});

		base += 4;
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

	setVertexAttribArray(5, effectIds);
	setVertexAttribBinding(5, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(6, gradientIds);
	setVertexAttribBinding(6, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(7, gradientXforms);
	setVertexAttribBinding(7, osg::Geometry::BIND_PER_VERTEX);

	addPrimitiveSet(indices);
}

void BoxDrawable::compile() {
	auto* atlas = getAtlas();

	if(!atlas || !atlas->isBuilt() || _layers.empty()) return;

	auto vertices = osgx::make_ref<osgx::Vec3Array>();
	auto colors = osgx::make_ref<osgx::Vec4Array>();
	auto emCoords = osgx::make_ref<osgx::Vec2Array>();
	auto bandXform = osgx::make_ref<osgx::Vec4Array>();
	auto shapeData = osgx::make_ref<osgx::Vec4Array>();
	auto effectIds = osgx::make_ref<osgx::FloatArray>();
	auto gradientIds = osgx::make_ref<osgx::FloatArray>();
	auto gradientXforms = osgx::make_ref<osgx::Vec4Array>();
	// auto indices = osgx::make_ref<osgx::DrawElementsUShort>(osg::PrimitiveSet::TRIANGLES);
	auto indices = osgx::make_ref<osgx::DrawElementsUShort>();

	auto addFace = [&](Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, size_t layerIdx) {
		const auto& layer = _layers[layerIdx];
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
		const auto eid = cv(layer.effectId);
		const auto gid = cv(layer.gradientId);

		osg::Vec4 gx(0.0f, 0.0f, 0.0f, 0.0f);

		if(layer.gradientId > 0) {
			const auto& grad = atlas->getGradients()[layer.gradientId - 1];
			const auto& m = grad.transform;

			if(grad.type == slughorn::GradientInfo::Type::Radial) {
				const float deltaR = m.xx - grad.innerRadius;
				const float invDeltaR = deltaR > 1e-6f ? 1.0f / deltaR : 0.0f;

				gx = { m.dx, m.dy, grad.innerRadius * invDeltaR, invDeltaR };
			}

			else if(grad.type == slughorn::GradientInfo::Type::Sweep) {
				const float arcSpan = m.xy;
				const float invArcSpan = arcSpan > 1e-6f ? 1.0f / arcSpan : 0.0f;

				gx = { m.dx, m.dy, m.xx, -invArcSpan }; // w < 0 = sweep
			}

			else gx = { m.xx, m.xy, m.dx, 0.0f };
		}

		auto base = static_cast<index_element_type>(vertices->size());

		Vec3 ps[4] = {p0, p1, p2, p3};

		slug_t uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

		for(size_t i = 0; i < 4; i++) {
			vertices->push_back(ps[i]);

			float emX = emX0 + uvs[i][0] * (emX1 - emX0);
			float emY = emY0 + uvs[i][1] * (emY1 - emY0);

			emCoords->push_back({emX, emY});
			colors->push_back(color);
			bandXform->push_back(bx);
			shapeData->push_back(sd);
			effectIds->push_back(eid);
			gradientIds->push_back(gid);
			gradientXforms->push_back(gx);
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

	setVertexAttribArray(5, effectIds);
	setVertexAttribBinding(5, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(6, gradientIds);
	setVertexAttribBinding(6, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(7, gradientXforms);
	setVertexAttribBinding(7, osg::Geometry::BIND_PER_VERTEX);

	addPrimitiveSet(indices);
}

void SubdividedDrawable::compile() {
	auto* atlas = getAtlas();

	if(!atlas || !atlas->isBuilt() || _layers.empty()) return;

	if(!_positionCallback) return;

	const auto* shape = atlas->getShape(_layers[0].key);

	if(!shape) return;

	auto vertices = osgx::make_ref<osgx::Vec3Array>();
	auto colors = osgx::make_ref<osgx::Vec4Array>();
	auto emCoords = osgx::make_ref<osgx::Vec2Array>();
	auto bandXform = osgx::make_ref<osgx::Vec4Array>();
	auto shapeData = osgx::make_ref<osgx::Vec4Array>();
	auto effectIds = osgx::make_ref<osgx::FloatArray>();
	auto gradientIds = osgx::make_ref<osgx::FloatArray>();
	auto gradientXforms = osgx::make_ref<osgx::Vec4Array>();
	auto indices = osgx::make_ref<osgx::DrawElementsUShort>();

	static constexpr slug_t SLUG_EXPAND = 0.01_cv;

	const slug_t emX0 = shape->bearingX - SLUG_EXPAND;
	const slug_t emY0 = (shape->bearingY - shape->height) - SLUG_EXPAND;
	const slug_t emX1 = (shape->bearingX + shape->width) + SLUG_EXPAND;
	const slug_t emY1 = shape->bearingY + SLUG_EXPAND;

	const Vec4 bx(
		shape->bandScaleX, shape->bandScaleY,
		shape->bandOffsetX, shape->bandOffsetY
	);

	const Vec4 sd(
		static_cast<slug_t>(shape->bandTexX), static_cast<slug_t>(shape->bandTexY),
		static_cast<slug_t>(shape->bandMaxX), static_cast<slug_t>(shape->bandMaxY)
	);

	const Vec4 color(
		_layers[0].color.r, _layers[0].color.g,
		_layers[0].color.b, _layers[0].color.a
	);

	const slug_t eid = static_cast<slug_t>(_layers[0].effectId);
	const slug_t gid = static_cast<slug_t>(_layers[0].gradientId);

	osg::Vec4 gx(0.0f, 0.0f, 0.0f, 0.0f);

	if(_layers[0].gradientId > 0) {
		const auto& grad = atlas->getGradients()[_layers[0].gradientId - 1];
		const auto& m = grad.transform;

		if(grad.type == slughorn::GradientInfo::Type::Radial) {
			const float deltaR = m.xx - grad.innerRadius;
			const float invDeltaR = deltaR > 1e-6f ? 1.0f / deltaR : 0.0f;
			gx = { m.dx, m.dy, grad.innerRadius * invDeltaR, invDeltaR };
		}

		else gx = { m.xx, m.xy, m.dx, 0.0f };
	}

	// Vertex grid: (_stepsV+1) rows x (_stepsU+1) cols
	for(size_t sv = 0; sv <= _stepsV; sv++) {
		const slug_t v = static_cast<slug_t>(sv) / static_cast<slug_t>(_stepsV);

		for(size_t su = 0; su <= _stepsU; su++) {
			const slug_t u = static_cast<slug_t>(su) / static_cast<slug_t>(_stepsU);

			vertices->push_back(_positionCallback(u, v));

			// u,v map linearly into em bounding box
			emCoords->push_back({
				emX0 + u * (emX1 - emX0),
				emY0 + v * (emY1 - emY0)
			});

			colors->push_back(color);
			bandXform->push_back(bx);
			shapeData->push_back(sd);
			effectIds->push_back(eid);
			gradientIds->push_back(gid);
			gradientXforms->push_back(gx);
		}
	}

	// Ensure we don't have TOO MANY indices!
	// TODO: We need more checks like this, as well as our own exceptions!
	if(
		(static_cast<std::size_t>(_stepsU + 1) * static_cast<std::size_t>(_stepsV + 1)) >
		static_cast<std::size_t>(std::numeric_limits<index_element_type>::max()) + 1
	) {
		throw std::runtime_error("Subdivided mesh exceeds index capacity");
	}

	// Index stitching; identical pattern for every subdivided mesh
	for(index_element_type sv = 0; sv < _stepsV; sv++) {
		for(index_element_type su = 0; su < _stepsU; su++) {
			const auto row0 = static_cast<index_element_type>(sv * (_stepsU + 1));
			const auto row1 = static_cast<index_element_type>((sv + 1) * (_stepsU + 1));
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

	setVertexAttribArray(5, effectIds);
	setVertexAttribBinding(5, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(6, gradientIds);
	setVertexAttribBinding(6, osg::Geometry::BIND_PER_VERTEX);

	setVertexAttribArray(7, gradientXforms);
	setVertexAttribBinding(7, osg::Geometry::BIND_PER_VERTEX);

	addPrimitiveSet(indices);
}

}
