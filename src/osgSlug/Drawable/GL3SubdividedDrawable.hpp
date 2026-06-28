#pragma once

#include "osgSlug/Drawable/SubdividedDrawable.hpp"

namespace osgSlug {

// GL3SubdividedDrawable: 8-attribute vertex path for subdivided meshes, GL 3.x compatible.
class GL3SubdividedDrawable: public SubdividedDrawable {
public:
	GL3SubdividedDrawable() = default;

	void compile() override;
	void setLayerEffectParam(size_t index, slug_t param) override;
};

}
