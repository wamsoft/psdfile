set(TPSTUB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../tp_stub CACHE PATH "Path to tp_stub directory")
include(${TPSTUB_DIR}/krkrz.cmake)

add_subdirectory(psdparse)

krkrz_plugin(psdfile
	NCBIND
	SOURCES
		psdclass.cpp
		psdclass_loadmem.cpp
		psdclass_loadstream.cpp
		psdclass_loadstreambase.cpp
		stdafx.cpp
		main.cpp
	LIBRARIES
		psdparse
)

if(MSVC)
	# MSVC用設定
	set_source_files_properties(
		psdclass_loadmem.cpp
		psdclass_loadstream.cpp
		PROPERTIES COMPILE_FLAGS "/Od"
	)
endif()
