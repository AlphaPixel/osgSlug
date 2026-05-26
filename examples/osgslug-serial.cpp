// vimrun! ./osgslug-serial

// TODO: This example is also the prototype for reusable .slug/.slugb ShapeDrawable loading. Factor
// the atlas/key/drawable creation into osgslug-example.hpp once another example needs the same
// path.

#include "osgslug-example.hpp"

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(
		args,
		"Load a Shape/CompositeShape from a .slug/.slugb file",
		{
			{
				"--shape <string>",
				"slughorn::Key corresponding to the Shape to render"
			},
			{
				"--composite-shape <string>",
				"slughorn::Key corresponding to the CompositeShape to render"
			}
		},
		1,
		"SLUG_FILE"
	)) return 1;

	std::string shape;
	std::string compositeShape;

	slughorn::Key key;

	while(args.read("--shape", shape)) key = example::parseKey(shape);
	while(args.read("--composite-shape", compositeShape)) key = example::parseKey(compositeShape);

	// if(!example::validatePositional(args, 1, "SLUG_FILE")) return example::fail(args, 2);

	osg::ref_ptr<osgSlug::Atlas> atlas;

	try {
		atlas = osgSlug::Atlas::read(args[1]);
	}

	catch(const std::exception& e) {
		OSG_WARN << e.what() << std::endl;

		return 2;
	}

	OSG_NOTICE << "Atlas loaded: "
		<< atlas->getShapes().size() << " shapes, "
		<< atlas->getCompositeShapes().size() << " composites" << std::endl
	;

	auto sd = example::makeShapeDrawable();

	if(shape.size()) {
		const slughorn::Atlas::Shape* s = atlas->getShape(key);

		if(s) {
			OSG_NOTICE << "Found Shape: w=" << s->width
				<< " h=" << s->height << std::endl
			;

			sd->addLayer({key, {1_cv, 1_cv, 1_cv, 1_cv}});
		}

		else return example::fail(args, 3, "Couldn't find Shape key in Atlas");
	}

	else if(compositeShape.size()) {
		const slughorn::CompositeShape* s = atlas->getCompositeShape(key);

		if(s) {
			OSG_NOTICE << "Found CompositeShape with "
				<< s->layers.size() << " layers" << std::endl
			;

			sd->addCompositeShape(*s);
		}

		else return example::fail(args, 4, "Couldn't find CompositeShape key in Atlas");
	}

	sd->setAtlas(atlas);
	sd->compile();

	auto sdg = osgx::make_ref<osg::Geode>();

	sdg->addDrawable(sd);
	sdg->setStateSet(atlas->createDefaultStateSet(example::USE_GL3));

	return example::run(viewer, args, sdg);
}
