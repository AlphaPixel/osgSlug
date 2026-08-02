cmake_minimum_required(VERSION 3.28)

project(osgSlug)

set(SLUGHORN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ext/slughorn" CACHE PATH
	"Path to the slughorn source tree (submodule or external checkout)")

if(NOT EXISTS "${SLUGHORN_DIR}/CMakeLists.txt")
	message(FATAL_ERROR
		"slughorn not found at SLUGHORN_DIR='${SLUGHORN_DIR}'.\n"
		"Run:  git submodule update --init --recursive\n"
		"Or:   cmake -DSLUGHORN_DIR=/path/to/slughorn"
	)
endif()

# include("etc/asan.cmake")
add_subdirectory("${SLUGHORN_DIR}" "ext/slughorn")

set(OSG_PYTHON_DIR "" CACHE PATH "Path to OpenSceneGraph.py checkout")

if(OSG_PYTHON_DIR)
	find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
	# TODO: Since BOTH PROJECTS have their own pybind11 checkouts, which should
	# we use?
	add_subdirectory("${SLUGHORN_DIR}/ext/pybind11" "ext/pybind11")
endif()

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

set(SKIA_DIR "" CACHE PATH "Path to Skia source tree (leave empty to skip Skia examples)")
set(SKIA_BUILD_DIR "" CACHE PATH "Path to Skia build directory containing libskia.a")

find_package(PkgConfig REQUIRED)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
pkg_check_modules(IMGUI imgui)

find_package(Freetype REQUIRED)
find_package(OpenGL QUIET)
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
find_package(osgx REQUIRED)

if(WIN32)
	set(OSGX_WARNING_OPTIONS
		/W4
		/WX
		/wd4100
		/analyze
	)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	set(OSGX_WARNING_OPTIONS
		-W
		-Wall
		-Wshadow
		-Wconversion
		-Wsign-conversion
		-Wfloat-conversion
		-Wpedantic
		-Wextra
		-Werror
		-Wno-unused-parameter
	)
else()
	set(OSGX_WARNING_OPTIONS)
endif()

if(SKIA_DIR AND SKIA_BUILD_DIR)
	add_library(skia STATIC IMPORTED)
	set_target_properties(skia PROPERTIES
		IMPORTED_LOCATION "${SKIA_BUILD_DIR}/libskia.a"
	)
	target_include_directories(skia INTERFACE "${SKIA_DIR}")
	set(OSGSLUG_HAS_SKIA TRUE)
else()
	set(OSGSLUG_HAS_SKIA FALSE)
	message(STATUS "Skia not configured — Skia examples will be skipped (set SKIA_DIR and SKIA_BUILD_DIR to enable)")
endif()

# Main library
add_library(osgSlug SHARED
	"src/Atlas.cpp"
	"src/Atlas.shaders.cpp"
	"src/Drawable.cpp"
	"src/Drawable/ShapeDrawable.cpp"
	"src/Drawable/BoxDrawable.cpp"
	"src/Drawable/SubdividedDrawable.cpp"
	"src/Drawable/DecalDrawable.cpp"
	"src/Drawable/PathDrawable.cpp"
	"src/Drawable/ScanlineDrawable.cpp"
	"src/Font.cpp"
	"src/Text.cpp"
)
set_target_properties(osgSlug PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(osgSlug PRIVATE cxx_std_20)
target_compile_options(osgSlug PRIVATE ${OSGX_WARNING_OPTIONS})
target_link_libraries(osgSlug PUBLIC
	${OPENSCENEGRAPH_LIBRARIES}
	Freetype::Freetype
	slughorn
	osgx::osgx
)
target_include_directories(osgSlug PUBLIC
	${OPENSCENEGRAPH_INCLUDE_DIRS}
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# OSG/osgDB ReaderWriter plugin
add_library(osgdb_slug SHARED "plugins/ReaderWriterSLUG.cpp")
target_compile_features(osgdb_slug PRIVATE cxx_std_20)
target_compile_options(osgdb_slug PRIVATE ${OSGX_WARNING_OPTIONS})
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
		target_compile_options(${name} PRIVATE ${OSGX_WARNING_OPTIONS})
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

		if(IMGUI_FOUND)
			target_compile_definitions(${name} PRIVATE OSGDEBUG_IMGUI)
			target_include_directories(${name} PRIVATE ${IMGUI_INCLUDE_DIRS})
			target_link_libraries(${name} PRIVATE ${IMGUI_LINK_LIBRARIES} OpenGL::GL)
		endif()
	endfunction()

	add_subdirectory("examples/tmp")

	# Standalone Blend2D Porter-Duff reference (no OSG; links only blend2d).
	# add_executable(bl-demo-porterduff "ext/bl_demo_porterduff.cpp")
	# target_compile_features(bl-demo-porterduff PRIVATE cxx_std_20)
	# target_include_directories(bl-demo-porterduff PRIVATE "/home/cubicool/dev/blend2d")
	# target_link_libraries(bl-demo-porterduff PRIVATE
	# 	"/home/cubicool/dev/blend2d/BUILD-g++-13.3.0-NOASAN/libblend2dd.so"
	# )

	osgslug_example(osgslug-ssbo SOURCES "examples/osgslug-ssbo.cpp")
	osgslug_example(osgslug-serial SOURCES "examples/osgslug-serial.cpp")
	osgslug_example(osgslug-text SOURCES "examples/osgslug-text.cpp")
	osgslug_example(osgslug-clock SOURCES "examples/osgslug-clock.cpp")
	osgslug_example(osgslug-hud SOURCES "examples/osgslug-hud.cpp")
	osgslug_example(osgslug-rtt SOURCES "examples/osgslug-rtt.cpp")
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
	osgslug_example(osgslug-shape-cairo
		SOURCES "examples/osgslug-shape-cairo.cpp"
		LIBS "PkgConfig::CAIRO"
	)
	osgslug_example(osgslug-compositeshape-canvas
		SOURCES "examples/osgslug-compositeshape-canvas.cpp"
	)
	if(OSGSLUG_HAS_SKIA)
		osgslug_example(osgslug-shape-skia
			SOURCES "examples/osgslug-shape-skia.cpp"
			LIBS "skia"
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
	endif()
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
	osgslug_example(osgslug-mask SOURCES "examples/osgslug-mask.cpp")
	osgslug_example(osgslug-mask-animation SOURCES "examples/osgslug-mask-animation.cpp")
	osgslug_example(osgslug-pbr-ibl SOURCES "examples/osgslug-pbr-ibl.cpp")
	osgslug_example(osgslug-pbr-ibl-text SOURCES "examples/osgslug-pbr-ibl-text.cpp")
endif()

if(OSG_PYTHON_DIR)
	pybind11_add_module(osgSlug_python "ext/osgSlug-python.cpp")
	set_target_properties(osgSlug_python PROPERTIES OUTPUT_NAME "osgSlug")
	target_include_directories(osgSlug_python PRIVATE
		"${OSG_PYTHON_DIR}"
		"${OSG_PYTHON_DIR}/etc"
	)
	target_link_libraries(osgSlug_python PRIVATE osgSlug pybind11::module)
	target_compile_features(osgSlug_python PUBLIC cxx_std_20)
	target_compile_options(osgSlug_python PRIVATE ${SLUGHORN_COMPILE_OPTIONS})
	set_target_properties(osgSlug_python PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
