#include "osgSlug/Drawable/BoxDrawable.hpp"

#include "Drawable/Util.hpp"

namespace osgSlug {

void BoxDrawable::compile() {
	if(_compiled) return;

	auto* atlas = getAtlas();

	if(!atlas) {
		OSG_WARN
			<< "BoxDrawable::compile(): no Atlas parent -- "
			<< "add to an Atlas node first (e.g. atlas->addChild(drawable))" << std::endl
		;

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
	auto indices = osgx::make_ref<index_type>();

	auto addFace = [&](Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, size_t index) {
		const auto& layer = _layers[index];
		const auto shape = atlas->getShape(layer.key);

		if(!shape) return;

		const slug_t expand = layer.expand;
		const auto [emX0, emY0, emX1, emY1] = computeEmBounds(*shape, expand);

		const Vec4 bx(shape->bandScaleX, shape->bandScaleY, shape->bandOffsetX, shape->bandOffsetY);
		const Vec4 sd(
			cv(shape->bandTexX), cv(shape->bandTexY),
			cv(shape->bandMaxX), cv(shape->bandMaxY)
		);
		const Vec4 color(layer.color.r, layer.color.g, layer.color.b, layer.color.a);
		const Vec4 eid(cv(layer.effectId), cv(shape->originX), cv(shape->originY), layer.effectParam);
		const slug_t lidx = cv(index + 1);

		const auto [gmeta, gxform] = buildGradientData(*atlas, layer);

		auto base = static_cast<index_element_type>(vertices->size());

		Vec3 ps[4] = {p0, p1, p2, p3};
		slug_t uvs[4][2] = {{0_cv, 0_cv}, {1_cv, 0_cv}, {1_cv, 1_cv}, {0_cv, 1_cv}};

		for(size_t i = 0; i < 4; i++) {
			vertices->push_back(Vec4(ps[i].x(), ps[i].y(), ps[i].z(), lidx));

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

	bindGL3Attribs(vertices, colors, emCoords, bandXform, shapeData, effectData, gradientMeta, gradientXforms);

	addPrimitiveSet(indices);

	_compiled = true;
}

}
