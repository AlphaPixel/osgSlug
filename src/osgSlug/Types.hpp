#pragma once

#include "slughorn/slughorn.hpp"
#include "osgx.hpp"

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

#include <charconv>

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

namespace detail {
	template<typename>
	inline constexpr bool always_false_v = false;

	template<typename... Values>
	bool isOneOf(std::string_view value, Values... values) {
		return ((value == values) || ...);
	}

	inline std::string makeEnvName(std::string_view name) {
		constexpr std::string_view prefix = "OSGSLUG_";

		if(name.starts_with(prefix)) return std::string(name);

		std::string result;

		result.reserve(prefix.size() + name.size());
		result.append(prefix);
		result.append(name);

		return result;
	}

	template<typename T>
	std::optional<T> parseEnvValue(std::string_view value) {
		if constexpr(std::is_same_v<T, std::string>) return std::string(value);

		else if constexpr(std::is_same_v<T, bool>) {
			if(detail::isOneOf(value, "1", "true", "TRUE", "yes", "YES", "on", "ON")) {
				return true;
			}

			if(detail::isOneOf(value, "0", "false", "FALSE", "no", "NO", "off", "OFF")) {
				return false;
			}

			return std::nullopt;
		}

		else if constexpr(std::is_integral_v<T>) {
			T result{};

			const char* first = value.data();
			const char* last = value.data() + value.size();

			auto [ptr, ec] = std::from_chars(first, last, result);

			if(ec != std::errc{} || ptr != last) return std::nullopt;

			return result;
		}

		else if constexpr(std::is_floating_point_v<T>) {
			// Floating-point std::from_chars exists, but support has historically
			// been uneven enough that strtod is still less annoying here.
			std::string tmp(value);

			char* end = nullptr;
			const double parsed = std::strtod(tmp.c_str(), &end);

			if(end == tmp.c_str() || *end != '\0') return std::nullopt;

			return static_cast<T>(parsed);
		}

		else {
			static_assert(always_false_v<T>, "Unsupported getEnv<T>() type");
		}
	}
}

template<typename T>
std::optional<T> getEnv(std::string_view name) {
	const std::string envName = detail::makeEnvName(name);

	const char* raw = std::getenv(envName.c_str());

	if(!raw) return std::nullopt;

	auto value = detail::parseEnvValue<T>(raw);

	if(!value) {
		OSG_WARN
			<< "Invalid " << envName << " value '" << raw
			<< "'; ignoring..."
			<< std::endl
		;

		return std::nullopt;
	}

	OSG_NOTICE
		<< envName << "=" << raw
		<< " detected..."
		<< std::endl
	;

	return value;
}

template<typename T>
T getEnv(std::string_view name, T fallback) {
	return getEnv<T>(name).value_or(fallback);
}

}
