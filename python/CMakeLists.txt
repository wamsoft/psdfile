cmake_minimum_required(VERSION 3.16)

# pybind11 は pip 経由 (`pip install pybind11`) で導入。
# Python は modern FindPython で探す (pybind11 の旧 FindPythonInterp 経路は
# 1) msys2 の python を引きやすい、2) CMP0148 で deprecated、なのでバイパス)。
if(NOT TARGET pybind11::module)
    set(PYBIND11_FINDPYTHON ON CACHE BOOL "" FORCE)

    # まず Python を確定させる。CMake が見つけたインタプリタを pybind11 が継承。
    if(NOT Python3_EXECUTABLE)
        # venv が活きていれば PATH 上のがそれ
        find_program(_psd_python NAMES python python3 REQUIRED)
        set(Python3_EXECUTABLE "${_psd_python}" CACHE FILEPATH "")
    endif()
    find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

    # pybind11 cmakedir を pip 経由で問い合わせる
    if(NOT pybind11_DIR)
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" -m pybind11 --cmakedir
            OUTPUT_VARIABLE _pybind11_cmakedir
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _pybind11_rv)
        if(NOT _pybind11_rv EQUAL 0)
            message(FATAL_ERROR
                "Could not locate pybind11 via '${Python3_EXECUTABLE} -m pybind11 --cmakedir'. "
                "Install with: pip install pybind11")
        endif()
        set(pybind11_DIR "${_pybind11_cmakedir}" CACHE PATH "pybind11 cmake dir")
    endif()
    find_package(pybind11 CONFIG REQUIRED)
endif()

pybind11_add_module(psdparse_py MODULE psdparse_module.cpp)
set_target_properties(psdparse_py PROPERTIES OUTPUT_NAME "psdparse")
target_link_libraries(psdparse_py PRIVATE psdparse)
