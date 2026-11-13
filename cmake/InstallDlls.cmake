# ---------------------------------------------------------------------------
#  安装阶段：把 exe 和 gdk-pixbuf 加载器的运行时依赖（DLL）全部拷进安装目录。
#
#  调用方（cmake/FollowRuntime.cmake 里的 install(CODE ...)）会先设好：
#    FOLLOW_EXE, FOLLOW_MINGW_BIN, FOLLOW_PIXBUF_BINDIR, FOLLOW_PIXBUF_RELDIR,
#    FOLLOW_INSTALL_PREFIX (= CMAKE_INSTALL_PREFIX)
#
#  用 CMake 自带的 file(GET_RUNTIME_DEPENDENCIES) 解析 PE 导入表（不依赖 ldd），
#  把 MSYS2 前缀里的依赖递归拉全：GTK / FFmpeg / GStreamer / curl / gnutls /
#  libgcc / libwinpthread 等等。
#
#  解析不了的东西分两类，处理方式不同：
#    * api-ms-win-* / ext-ms-*   Windows API set 转发 DLL，系统自带，忽略
#    * 别的名字                  先看看 MSYS2 前缀里有没有同名文件，有就补拷；
#                                没有就当它是 Windows 内部 DLL（ntdll 的依赖之类）
# ---------------------------------------------------------------------------

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

# Windows 自带的系统 DLL 不打包（KERNEL32 / USER32 / ucrtbase ...）
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

    # 加载器已经由 install(DIRECTORY) 装到 lib/gdk-pixbuf-2.0/.../loaders，别重复拷
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

# 解析不了的名字：MSYS2 前缀里有同名文件的补拷，其余当 Windows 内部 DLL 略过
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
