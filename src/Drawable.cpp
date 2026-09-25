#include "osgSlug/Drawable.hpp"

namespace osgSlug {

RenderMask::RenderMask(const slughorn::Mask& mask):
_mask(mask) {
	_data = new osg::UByteArray(sizeof(PackedData));

	_data->setBufferObject(new osg::UniformBufferObject());

	pack(nullptr);

	_binding = new osg::UniformBufferBinding(
		osgx::Library::instance().bindings().get("osgSlug::mask"),
		_data.get(),
		0,
		sizeof(PackedData)
	);
}

osg::ref_ptr<RenderMask> RenderMask::createNull() {
	osg::ref_ptr<RenderMask> mask = new RenderMask(slughorn::Mask{});

	mask->_null = true;
	mask->pack(nullptr);

	return mask;
}

void RenderMask::repack(const Atlas& atlas) {
	pack(&atlas);
}

void RenderMask::apply(osg::State& state) const {
	_binding->apply(state);
}

void RenderMask::pack(const Atlas* atlas) {
	PackedData d;

	// The sentinel: every other field stays at PackedData's zero-init default (harmless --
	// nothing reads them once type<0 short-circuits osgSlug_Mask_CoverageFor).
	if(_null) {
		d.type = -1;

		std::memcpy(&(*_data)[0], &d, sizeof(d));

		_data->dirty();

		return;
	}

	// params/params2: raw copy. Authoring owns these for every type - including SDFTile's
	// ox/oy (the canvas-space position of the tile shape's em origin; see the Mask comment in
	// slughorn.hpp), which the baked tile cannot know on its own.
	d.params[0] = _mask.params[0];
	d.params[1] = _mask.params[1];
	d.params[2] = _mask.params[2];
	d.params[3] = _mask.params[3];
	d.params2[0] = _mask.params[4];
	d.params2[1] = _mask.params[5];

	d.type = static_cast<int32_t>(_mask.type);
	d.invert = _mask.invert ? 1 : 0;
	d.debug = _debug ? 1 : 0;

	// The tile itself is assigned internally by Atlas::requestSDF() - the one thing here the
	// author genuinely cannot supply, so it's the one case pack() resolves itself. Left zeroed
	// (rect.zw == 0) when there is no atlas yet or no tile, which the shader reads as "none".
	if(atlas && _mask.type == slughorn::Mask::Type::SDFTile && _mask.key) {
		if(const auto shape = atlas->getShape(*_mask.key); shape && shape->sdf) {
			const auto& t = *shape->sdf;

			d.sdfRect[0] = cv(t.x);
			d.sdfRect[1] = cv(t.y);
			d.sdfRect[2] = cv(t.w);
			d.sdfRect[3] = cv(t.h);
			d.sdfFrame[0] = t.emOriginX;
			d.sdfFrame[1] = t.emOriginY;
			d.sdfFrame[2] = t.texelsPerEm;
			d.sdfFrame[3] = t.range;
		}
	}

	std::memcpy(&(*_data)[0], &d, sizeof(d));

	_data->dirty();
}

Drawable::Drawable() {
	setUseDisplayList(false);
	setUseVertexBufferObjects(true);
}

Atlas* Drawable::getAtlas() const {
	if(_atlas) return _atlas.get();

	return osgx::getFirstParent<Atlas>(this);
}

void Drawable::compileGLObjects(osg::RenderInfo& renderInfo) const {
	if(getAtlas()) const_cast<Drawable*>(this)->compile();

	osg::Geometry::compileGLObjects(renderInfo);
}

}
