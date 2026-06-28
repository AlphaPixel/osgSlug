#include "osgSlug/Drawable.hpp"

namespace osgSlug {

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
