# This file simply picks up ANY file matching the glob below and tries to
# compile it; this is where I do my "scratchpad" tests (quick one-off
# experiments that sometimes get "promoted" to REAL examples...)
file(GLOB TMP_SOURCES CONFIGURE_DEPENDS "osgslug-tmp.*.cpp")

foreach(source ${TMP_SOURCES})
	cmake_path(GET source STEM LAST_ONLY name)

	# osgx::gltf_pbribl (PBRIBL.cpp) is its own static lib target, separate from osgx::osgx --
	# osgSlug only links the latter, so anything calling into osgx::gltf::pbribl needs this added
	# explicitly or it fails at link time with undefined references, not compile time.
	if(name STREQUAL "osgslug-tmp.decalcube")
		osgslug_example(${name} SOURCES "${source}" LIBS "osgx::gltf_pbribl")
	else()
		osgslug_example(${name} SOURCES "${source}")
	endif()
endforeach()
