cmake_minimum_required(VERSION 3.16)

# UTF-8 source
add_compile_options("$<$<AND:$<C_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:C>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Zc:__cplusplus>")

set(PROJECT_NAME psdparse)
set(PROJECT_VERSION 1.00)

project(${PROJECT_NAME} VERSION ${PROJECT_VERSION} LANGUAGES CXX)

find_package(ZLIB REQUIRED)

add_library(${PROJECT_NAME} STATIC
	bmp.cpp
	psddesc.cpp
	psdfile.cpp
	psdimage.cpp
	psdlayer.cpp
	psdparse.h
	psdparse.cpp
	psdresource.cpp
	psdwrite.cpp
	stdafx.cpp
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

target_include_directories(${PROJECT_NAME} PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
	ZLIB::ZLIB
)

# スタンドアロン CLI (PSD をロードしてヘッダを表示する smoke test 用)
option(PSDPARSE_BUILD_CLI "Build psdparse standalone CLI for smoke testing" OFF)
if(PSDPARSE_BUILD_CLI)
	add_executable(psdparse_cli psd_cli.cpp)
	target_link_libraries(psdparse_cli PRIVATE psdparse)
endif()
