#include "osgSlug/Drawable.hpp"

namespace osgSlug {

RenderMask::RenderMask(const slughorn::Mask& mask, unsigned bindingPoint):
_mask(mask) {
	_data = new osg::UByteArray(sizeof(PackedData));
	_data->setBufferObject(new osg::UniformBufferObject());

	pack(nullptr);

	_binding = new osg::UniformBufferBinding(bindingPoint, _data.get(), 0, sizeof(PackedData));
}

void RenderMask::repack(const Atlas& atlas) {
	pack(&atlas);
}

void RenderMask::apply(osg::State& state) const {
	_binding->apply(state);
}

void RenderMask::pack(const Atlas* atlas) {
	PackedData d;

	// params/params2: raw copy. Authoring owns these for every type -- including MSDF's
	// cx/cy/r/range (see the Mask::Type::MSDF comment in slughorn.hpp); nothing here can
	// derive that bbox on the author's behalf, per slughorn's "raw data out, frontend
	// decides" contract.
	d.params[0] = _mask.params[0];
	d.params[1] = _mask.params[1];
	d.params[2] = _mask.params[2];
	d.params[3] = _mask.params[3];
	d.params2[0] = _mask.params[4];
	d.params2[1] = _mask.params[5];

	d.type = static_cast<int32_t>(_mask.type);
	d.invert = _mask.invert ? 1 : 0;
	d.debug = _debug ? 1 : 0;

	// msdfLayer is assigned internally by Atlas::registerMSDF() -- the only field here the
	// author genuinely cannot supply, so it's the one case pack() resolves itself.
	d.msdfLayer = -1;

	if(atlas && _mask.type == slughorn::Mask::Type::MSDF && _mask.key) {
		if(const auto shape = atlas->getShape(*_mask.key)) d.msdfLayer = shape->msdfLayer;
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
