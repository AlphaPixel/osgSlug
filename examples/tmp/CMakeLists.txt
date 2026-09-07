# This file simply picks up ANY file matching the glob below and tries to
# compile it; this is where I do my "scratchpad" tests (quick one-off
# experiments that sometimes get "promoted" to REAL examples...)
file(GLOB TMP_SOURCES CONFIGURE_DEPENDS "osgslug-tmp.*.cpp")

# tilestream reads real MVT tiles out of a local .mbtiles (SQLite3, gzipped protobuf blobs) -
# same dependencies as slughorn/test/slughorn-test-mapbox.cpp.
find_package(SQLite3)
find_package(ZLIB)

foreach(source ${TMP_SOURCES})
	cmake_path(GET source STEM LAST_ONLY name)

	# osgx::gltf_pbribl (PBRIBL.cpp) is its own static lib target, separate from osgx::osgx --
	# osgSlug only links the latter, so anything calling into osgx::gltf::pbribl needs this added
	# explicitly or it fails at link time with undefined references, not compile time.
	if(name STREQUAL "osgslug-tmp.decalcube" OR name STREQUAL "osgslug-tmp.liticons")
		osgslug_example(${name} SOURCES "${source}" LIBS "osgx::gltf_pbribl")
	else()
		osgslug_example(${name} SOURCES "${source}")
	endif()

	# splatting reads real MVT tiles (vtzero/protozero, siblings of osgSlug under ~/dev) for
	# the --inkdrawable whole-tile PathDrawable::setPaths() test.
	if(name STREQUAL "osgslug-tmp.splatting")
		target_include_directories(${name} PRIVATE
			"${CMAKE_CURRENT_SOURCE_DIR}/../../../vtzero/include"
			"${CMAKE_CURRENT_SOURCE_DIR}/../../../protozero/include"
		)
	endif()

	# tilestream: real MVT tiles out of a local .mbtiles - see comment above.
	if(name STREQUAL "osgslug-tmp.tilestream")
		target_include_directories(${name} PRIVATE
			"${CMAKE_CURRENT_SOURCE_DIR}/../../../vtzero/include"
			"${CMAKE_CURRENT_SOURCE_DIR}/../../../protozero/include"
		)

		if(SQLite3_FOUND AND ZLIB_FOUND)
			target_link_libraries(${name} PRIVATE SQLite::SQLite3 ZLIB::ZLIB)
		else()
			message(STATUS "osgSlug examples: SQLite3/ZLIB not found, skipping osgslug-tmp.tilestream link deps")
		endif()
	endif()
endforeach()
