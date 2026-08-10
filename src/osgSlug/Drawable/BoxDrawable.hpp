#pragma once

#include "osgSlug/Drawable/ShapeDrawable.hpp"

#include <array>
#include <stdexcept>

namespace osgSlug {

// A unit cube where each of the 6 faces renders its own slughorn::CompositeShape -- i.e. each
// face can stack multiple layers (e.g. a background plate plus an overlay glyph/pip pattern),
// not just a single shape. All layers within one face share that face's quad/plane; compile()
// nudges each layer slightly outward along the face normal in emission order to avoid z-fighting
// between coincident geometry.
class BoxDrawable: public ShapeDrawable {
public:
	BoxDrawable() = default;

	// face is 0-5, matching compile()'s own face order: 0=+Z, 1=-Z, 2=+X, 3=-X, 4=+Y, 5=-Y.
	void setFace(size_t face, const slughorn::CompositeShape& composite) {
		if(face >= 6) throw std::out_of_range("BoxDrawable::setFace(): face must be 0-5");

		_faces[face] = composite;
	}

	void compile() override;

private:
	// Per-face content replaces ShapeDrawable's single-layer-per-addLayer() model entirely.
	// Disabled (rather than silently accepted and ignored by compile()) since a stray
	// addLayer()/addCompositeShape() call would otherwise populate the inherited _layers with
	// data compile() never reads -- see the Font/error-handling convention of failing loud at
	// the point of misuse rather than producing a silently-blank face later.
	void addLayer(const slughorn::Layer&) override {
		throw std::logic_error(
			"BoxDrawable::addLayer(): use setFace() instead -- each face is a CompositeShape, not a single Layer"
		);
	}

	void addCompositeShape(const slughorn::CompositeShape&) override {
		throw std::logic_error(
			"BoxDrawable::addCompositeShape(): use setFace() instead -- pass one CompositeShape per face index"
		);
	}

	slug_t _size = 1_cv;
	std::array<slughorn::CompositeShape, 6> _faces;
};

}
