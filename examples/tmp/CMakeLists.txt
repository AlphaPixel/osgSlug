# This file simply picks up ANY file matching the glob below and tries to
# compile it; this is where I do my "scratchpad" tests (quick one-off
# experiments that sometimes get "promoted" to REAL examples...)
file(GLOB TMP_SOURCES CONFIGURE_DEPENDS "osgslug-tmp.*.cpp")

foreach(source ${TMP_SOURCES})
	cmake_path(GET source STEM LAST_ONLY name)

	osgslug_example(${name} SOURCES "${source}")

	# TODO: Implement this! We need some way to indicate whether the scratchpad
	# example needs additional flags!
	# if(name STREQUAL "osgslug-tmp.cairo.*")
	# 	osgslug_example(${name} SOURCES "${source}" LIBS "PkgConfig::CAIRO" "skia")
	# else()
	# 	osgslug_example(${name} SOURCES "${source}")
	# endif()
endforeach()
