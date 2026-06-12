cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_compile_options("$<$<AND:$<C_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:C>>:/utf-8>")
    add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/utf-8>")
    add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Zc:__cplusplus>")
    project(psdfile LANGUAGES CXX)
endif()

# psdparse: pure C++ library (no kirikiri / tp_stub dependency)
add_subdirectory(psdparse)

# Python バインディング (pip 経由 pybind11)
option(PSDFILE_BUILD_PYTHON "Build pybind11 Python bindings" OFF)
if(PSDFILE_BUILD_PYTHON)
    add_subdirectory(python)
endif()

# 吉里吉里プラグイン側のビルドは tp_stub / ncbind が用意されているときだけ。
# Python での動作確認が優先のため、ここで gating する。
set(_default_build_plugin OFF)
if(DEFINED TPSTUB_DIR AND EXISTS "${TPSTUB_DIR}/krkrz.cmake")
    set(_default_build_plugin ON)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../tp_stub/krkrz.cmake")
    set(_default_build_plugin ON)
endif()
option(PSDFILE_BUILD_KIRIKIRI_PLUGIN
       "Build the kirikiri (krkrz) plugin (requires tp_stub and ncbind)"
       ${_default_build_plugin})

if(PSDFILE_BUILD_KIRIKIRI_PLUGIN)
    if(NOT DEFINED TPSTUB_DIR)
        set(TPSTUB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../tp_stub
            CACHE PATH "Path to tp_stub directory")
    endif()
    include(${TPSTUB_DIR}/krkrz.cmake)

    krkrz_plugin(psdfile
        NCBIND
        SOURCES
            psdclass.cpp
            psdclass_loadstream.cpp
            stdafx.cpp
            main.cpp
        LIBRARIES
            psdparse
    )

endif()
