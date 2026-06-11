cmake_minimum_required(VERSION 3.28)

project(osgSlug)

# if(NOT EXISTS "${CMAKE_SOURCE_DIR}/ext/slughorn/slughorn.hpp")
# 	message(FATAL_ERROR
# 		"slughorn submodule is missing. Run:\n"
# 		"  git submodule update --init --recursive"
# 	)
# endif()

# include("etc/asan.cmake")
# add_subdirectory("ext/slughorn")
add_subdirectory("/home/cubicool/dev/slughorn" "ext/slughorn")
add_subdirectory("/home/cubicool/dev/slughorn/ext/pybind11" "ext/pybind11")

# # TODO CONVERT ALL `add_subdirectory` bits to something like this!
# function(ext_cli11)
# 	set(BUILD_TESTING OFF CACHE INTERNAL "")
# 	set(CLI11_BUILD_EXAMPLES OFF CACHE INTERNAL "")
# 	set(CLI11_BUILD_TESTS OFF CACHE INTERNAL "")
# 	set(CLI11_INSTALL OFF CACHE INTERNAL "")
# 	set(CLI11_PRECOMPILED ON CACHE INTERNAL "")
#
# 	add_subdirectory("ext/CLI11" EXCLUDE_FROM_ALL)
# endfunction()

# ext_cli11()

set(SKIA_DIR "/home/cubicool/dev/skia")
set(SKIA_BUILD_DIR "/home/cubicool/dev/skia/out/Release")

find_package(PkgConfig REQUIRED)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)

find_package(Freetype REQUIRED)
# find_package(harfbuzz REQUIRED)
find_package(OpenSceneGraph REQUIRED
	osg
	osgDB
	osgGA
	osgViewer
	osgUtil
	osgFX
	osgText
	osgWidget
	osgAnimation
)

if(WIN32)
	set(OSGSLUG_COMPILE_OPTIONS
		"/W4"
		"/WX"
		"/wd4100"
		"/analyze"
		# "/wd4225"
		# "/wd4244"
		# "/wd4668"
		# "/wd4146"
		# "/wd4267"
		# "/wd4996"
		# "/wd4334"
		# "/wd4311"
	)

else(WIN32)
	if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
		set(OSGSLUG_COMPILE_OPTIONS
			"-W"
			"-Wall"
			"-Wshadow"
			"-Wconversion"
			"-Wpedantic"
			"-Wextra"
			"-Werror"
			"-Wno-unused-parameter"
			#"-Wno-conversion"
			#"-Wno-missing-field-initializers"
			#"-Wno-implicit-fallthrough"
			#"-Wno-shadow"
			#"-Wno-empty-body"
			#"-Wno-sign-compare"
		)
	else(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		set(OSGSLUG_COMPILE_OPTIONS
			"-W"
			"-Wall"
			"-Wshadow"
			"-Wconversion"
			"-Wpedantic"
			"-Wextra"
			"-Werror"
			"-Wno-unused-parameter"
			#"-Wno-conversion"
			#"-Wno-missing-field-initializers"
			#"-Wno-implicit-fallthrough"
			#"-Wno-shadow"
			#"-Wno-empty-body"
			#"-Wno-sign-compare"
		)
	endif()
endif()

add_library(skia STATIC IMPORTED)
set_target_properties(skia PROPERTIES
	IMPORTED_LOCATION "${SKIA_BUILD_DIR}/libskia.a"
)
target_include_directories(skia INTERFACE "${SKIA_DIR}")

# Main library
add_library(osgSlug
	"src/Atlas.cpp"
	"src/Drawable.cpp"
	"src/InkDrawable.cpp"
	"src/Font.cpp"
	"src/Text.cpp"
)
set_target_properties(osgSlug PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(osgSlug PRIVATE cxx_std_20)
target_compile_options(osgSlug PRIVATE ${OSGSLUG_COMPILE_OPTIONS})
target_link_libraries(osgSlug PUBLIC
	${OPENSCENEGRAPH_LIBRARIES}
	Freetype::Freetype
	slughorn
)
target_include_directories(osgSlug PUBLIC
	${OPENSCENEGRAPH_INCLUDE_DIRS}
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	# TODO: REMOVE THIS!
	"${CMAKE_CURRENT_SOURCE_DIR}/ext/osgdebug"
)

# OSG/osgDB ReaderWriter plugin
add_library(osgdb_slug SHARED "plugins/ReaderWriterSLUG.cpp")
target_compile_features(osgdb_slug PRIVATE cxx_std_20)
target_compile_options(osgdb_slug PRIVATE ${OSGSLUG_COMPILE_OPTIONS})
target_link_libraries(osgdb_slug PRIVATE osgSlug)
target_include_directories(osgdb_slug PRIVATE
	${OPENSCENEGRAPH_INCLUDE_DIRS}
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)

option(OSGSLUG_BUILD_EXAMPLES "Build example programs" OFF)

if(OSGSLUG_BUILD_EXAMPLES)
	function(osgslug_example name)
		set(options)
		set(oneValueArgs)
		set(multiValueArgs SOURCES LIBS)

		cmake_parse_arguments(OSGSLUG_EXAMPLE
			"${options}"
			"${oneValueArgs}"
			"${multiValueArgs}"
			${ARGN}
		)

		add_executable(${name} ${OSGSLUG_EXAMPLE_SOURCES})

		target_compile_features(${name} PRIVATE cxx_std_20)
		target_compile_options(${name} PRIVATE ${OSGSLUG_COMPILE_OPTIONS})
		target_link_libraries(${name} PRIVATE
			osgSlug
			${OSGSLUG_EXAMPLE_LIBS}
			# CLI11::CLI11
		)
		target_include_directories(${name} PRIVATE
			"${CMAKE_CURRENT_SOURCE_DIR}/src"
			"${CMAKE_SOURCE_DIR}/examples"
			# "${CMAKE_SOURCE_DIR}/ext/nanosvg/src"
		)
	endfunction()

	add_subdirectory("examples/tmp")

	osgslug_example(osgslug-ssbo SOURCES "examples/osgslug-ssbo.cpp")
	osgslug_example(osgslug-serial SOURCES "examples/osgslug-serial.cpp")
	osgslug_example(osgslug-text SOURCES "examples/osgslug-text.cpp")
	osgslug_example(osgslug-clock SOURCES "examples/osgslug-clock.cpp")
	osgslug_example(osgslug-hud SOURCES "examples/osgslug-hud.cpp")
	osgslug_example(osgslug-ammohud SOURCES "examples/osgslug-ammohud.cpp")
	osgslug_example(osgslug-compute SOURCES "examples/osgslug-compute.cpp")
	osgslug_example(osgslug-gradient-canvas SOURCES "examples/osgslug-gradient-canvas.cpp")
	osgslug_example(osgslug-simple SOURCES "examples/osgslug-simple.cpp")
	osgslug_example(osgslug-simple-3d SOURCES "examples/osgslug-simple-3d.cpp")
	osgslug_example(osgslug-simple-animation SOURCES "examples/osgslug-simple-animation.cpp")
	osgslug_example(osgslug-font-animation SOURCES "examples/osgslug-font-animation.cpp")
	osgslug_example(osgslug-origin SOURCES "examples/osgslug-origin.cpp")
	osgslug_example(osgslug-nanosvg SOURCES "examples/osgslug-nanosvg.cpp")
	osgslug_example(osgslug-emoji-ft2 SOURCES "examples/osgslug-emoji-ft2.cpp")
	osgslug_example(osgslug-emoji-grid SOURCES "examples/osgslug-emoji-grid.cpp")
	osgslug_example(osgslug-shape-canvas SOURCES "examples/osgslug-shape-canvas.cpp")
	osgslug_example(osgslug-shape-skia
		SOURCES "examples/osgslug-shape-skia.cpp"
		LIBS "skia"
	)
	osgslug_example(osgslug-shape-cairo
		SOURCES "examples/osgslug-shape-cairo.cpp"
		LIBS "PkgConfig::CAIRO"
	)
	osgslug_example(osgslug-compositeshape-canvas
		SOURCES "examples/osgslug-compositeshape-canvas.cpp"
	)
	osgslug_example(osgslug-compositeshape-skia
		SOURCES "examples/osgslug-compositeshape-skia.cpp"
		LIBS "skia"
	)
	osgslug_example(osgslug-compositeshape-skia-logo
		SOURCES "examples/osgslug-compositeshape-skia-logo.cpp"
		LIBS
			"skia"
			"PkgConfig::FONTCONFIG"
	)
	osgslug_example(osgslug-compositeshape-cairo
		SOURCES "examples/osgslug-compositeshape-cairo.cpp"
		LIBS "PkgConfig::CAIRO"
	)
	osgslug_example(osgslug-project2d
		SOURCES "examples/osgslug-project2d.cpp"
		LIBS "PkgConfig::CAIRO"
	)
	osgslug_example(osgslug-compositeshape-mixed
		SOURCES "examples/osgslug-compositeshape-mixed.cpp"
	)
endif()

pybind11_add_module(osgSlug_python "ext/osgSlug-python.cpp")

set_target_properties(osgSlug_python PROPERTIES OUTPUT_NAME "osgSlug")
target_include_directories(osgSlug_python PRIVATE
	"/home/cubicool/dev/OpenSceneGraph.py"
	"/home/cubicool/dev/OpenSceneGraph.py/etc"
)
target_link_libraries(osgSlug_python PRIVATE osgSlug pybind11::module)
target_compile_features(osgSlug_python PUBLIC cxx_std_20)
target_compile_options(osgSlug_python PRIVATE ${SLUGHORN_COMPILE_OPTIONS})
set_target_properties(osgSlug_python PROPERTIES POSITION_INDEPENDENT_CODE ON)
