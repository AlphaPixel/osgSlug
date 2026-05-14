#pragma once

#include "slughorn/slughorn.hpp"
#include "osgDebug.hpp"

#define OSGSLUG_DISABLE_WARNINGS \
	_Pragma("GCC diagnostic push") \
	_Pragma("GCC diagnostic ignored \"-Wconversion\"") \
	_Pragma("GCC diagnostic ignored \"-Wdeprecated-copy\"") \
	_Pragma("GCC diagnostic ignored \"-Wfloat-conversion\"") \
	_Pragma("GCC diagnostic ignored \"-Wsign-compare\"") \
	_Pragma("GCC diagnostic ignored \"-Woverloaded-virtual\"") \
	_Pragma("GCC diagnostic ignored \"-Wshadow\"") \
	_Pragma("GCC diagnostic ignored \"-Wunused-but-set-variable\"")

#define OSGSLUG_ENABLE_WARNINGS \
	_Pragma("GCC diagnostic pop")

OSGSLUG_DISABLE_WARNINGS

#include <osg/Vec4f>
#include <osg/Matrixf>

OSGSLUG_ENABLE_WARNINGS

constexpr const uint8_t OSGSLUG_VERSION_MAJOR = 0;
constexpr const uint8_t OSGSLUG_VERSION_MINOR = 0;
constexpr const uint8_t OSGSLUG_VERSION_PATCH = 1;

using namespace slughorn::literals;
using slughorn::slug_t;

namespace osgSlug {
	using Vec2 = osg::Vec2f;
	using Vec3 = osg::Vec3f;
	using Vec4 = osg::Vec4f;
	using Matrix = osg::Matrixf;
}
