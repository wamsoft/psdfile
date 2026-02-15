cmake_minimum_required(VERSION 3.16)

# UTF-8 source
add_compile_options("$<$<AND:$<C_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:C>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Zc:__cplusplus>")

# MSVC static runtime
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(PROJECT_NAME psdfile)
set(PROJECT_VERSION 1.00)

project(${PROJECT_NAME} VERSION ${PROJECT_VERSION})

add_subdirectory(psdparse)

if(TVP_STATIC_PLUGIN)
    set(TVP_LIBRARY_TYPE STATIC)
else()
    set(TVP_LIBRARY_TYPE SHARED)
endif()

add_library(${PROJECT_NAME} ${TVP_LIBRARY_TYPE}
    ${CMAKE_CURRENT_SOURCE_DIR}/../ncbind/ncbind.cpp
	psdclass.cpp
	psdclass_loadmem.cpp
	psdclass_loadstream.cpp
	psdclass_loadstreambase.cpp
	stdafx.cpp
	main.cpp
)

if (TVP_STATIC_PLUGIN)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    TVP_STATIC_PLUGIN
    TVP_PLUGIN_NAME=${PROJECT_NAME}
)
endif()

if(MSVC)
    # MSVC用設定
    set_source_files_properties(
		psdclass_loadmem.cpp
		psdclass_loadstream.cpp
		PROPERTIES COMPILE_FLAGS "/Od"
    )
endif()


target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../ncbind
)

target_link_libraries(${PROJECT_NAME} PUBLIC
	tp_stub
	psdparse
)
