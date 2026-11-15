# 安装阶段：递归收集 exe 与 gdk-pixbuf 加载器的 DLL 依赖。
# api-ms-win-* / ext-ms-* 为系统 API set，忽略。

if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)   # GET_RUNTIME_DEPENDENCIES 先规范化路径再匹配
endif()

if(NOT FOLLOW_EXE OR NOT EXISTS "${FOLLOW_EXE}")
    message(FATAL_ERROR "InstallDlls: FOLLOW_EXE 无效: '${FOLLOW_EXE}'")
endif()

file(GLOB _loaders "${FOLLOW_PIXBUF_BINDIR}/loaders/*.dll")

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES  "${FOLLOW_EXE}"
    LIBRARIES    ${_loaders}
    DIRECTORIES  "${FOLLOW_MINGW_BIN}" "${FOLLOW_PIXBUF_BINDIR}/loaders"
    PRE_EXCLUDE_REGEXES  "^(api|ext)-ms-.*"
    RESOLVED_DEPENDENCIES_VAR   _resolved
    UNRESOLVED_DEPENDENCIES_VAR _unresolved)

# 系统 DLL 不打包
set(_system_dirs)
foreach(_d "$ENV{SystemRoot}/System32" "$ENV{SystemRoot}/SysWOW64" "$ENV{SystemRoot}")
    if(_d)
        string(TOLOWER "${_d}" _d_lc)
        string(REPLACE "\\" "/" _d_lc "${_d_lc}")
        list(APPEND _system_dirs "${_d_lc}/")
    endif()
endforeach()

macro(_follow_install_dll _dep)
    file(INSTALL DESTINATION "${FOLLOW_INSTALL_PREFIX}" TYPE SHARED_LIBRARY FILES "${_dep}")
    math(EXPR _count "${_count} + 1")
endmacro()

set(_count 0)
set(_skipped 0)

foreach(_dep IN LISTS _resolved)
    get_filename_component(_name "${_dep}" NAME)
    get_filename_component(_dir  "${_dep}" DIRECTORY)
    string(TOLOWER "${_dir}" _dir_lc)
    string(REPLACE "\\" "/" _dir_lc "${_dir_lc}")


    if(_dep MATCHES "/loaders/" OR _dep MATCHES "\\\\loaders\\\\")
        continue()
    endif()

    set(_is_system FALSE)
    foreach(_sd IN LISTS _system_dirs)
        if(_dir_lc MATCHES "^${_sd}")
            set(_is_system TRUE)
        endif()
    endforeach()

    if(_is_system)
        math(EXPR _skipped "${_skipped} + 1")
        continue()
    endif()

    _follow_install_dll("${_dep}")
endforeach()


set(_unknown "")
foreach(_name IN LISTS _unresolved)
    if(_name MATCHES "^(api|ext)-ms-")
        math(EXPR _skipped "${_skipped} + 1")
        continue()
    endif()

    if(EXISTS "${FOLLOW_MINGW_BIN}/${_name}")
        message(STATUS "InstallDlls: 手动补上 ${_name}")
        _follow_install_dll("${FOLLOW_MINGW_BIN}/${_name}")
    else()
        list(APPEND _unknown "${_name}")
    endif()
endforeach()

message(STATUS "InstallDlls: 拷入 ${_count} 个 DLL，跳过 ${_skipped} 个系统 DLL")

if(_unknown)
    list(LENGTH _unknown _n)
    list(REMOVE_DUPLICATES _unknown)
    message(STATUS "InstallDlls: ${_n} 个 Windows 内部 DLL 没有对应文件，已忽略：${_unknown}")
endif()
