cmake_minimum_required(VERSION 3.16)
add_compile_options("$<$<AND:$<C_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:C>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Zc:__cplusplus>")
project(psdfile LANGUAGES CXX)

# psdparse: pure C++ library (吉里吉里非依存)。external/psdparse は git submodule。
# 初回 clone 後は `git submodule update --init` で取得。
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/psdparse/CMakeLists.txt")
    message(FATAL_ERROR
        "external/psdparse is empty. Run: git submodule update --init --recursive")
endif()
add_subdirectory(external/psdparse)

# 吉里吉里プラグイン本体。tp_stub と ncbind は隣接の D:\test\tp_stub /
# D:\test\ncbind を見る (TPSTUB_DIR 環境変数で上書き可)。
if(NOT DEFINED TPSTUB_DIR)
    set(TPSTUB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../tp_stub
        CACHE PATH "Path to tp_stub directory")
endif()

if(NOT EXISTS "${TPSTUB_DIR}/krkrz.cmake")
    message(FATAL_ERROR
        "tp_stub not found at ${TPSTUB_DIR}. Set TPSTUB_DIR or place tp_stub there.")
endif()
include(${TPSTUB_DIR}/krkrz.cmake)

krkrz_plugin(psdfile
    NCBIND
    SOURCES
        psdclass.cpp
        psdclass_loadstream.cpp
        main.cpp
        LicensesGen.cpp
    LIBRARIES
        psdparse
)
