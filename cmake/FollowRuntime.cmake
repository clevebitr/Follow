# ---------------------------------------------------------------------------
#  GTK 运行时数据 + DLL 收集（Windows）
#
#  只把 DLL 拷进包里是不够的，GTK 还依赖下面这些「数据」，
#  它们都是**环境变量无关**的：
#    etc/ssl/certs/              CA 证书（libcurl 默认路径写死在 MSYS2 目录里，
#                                src/net/src/http.c 会在启动时指到这份）
#    share/glib-2.0/schemas/     GSettings schema（glib 按 DLL 所在目录找）
#    share/icons/Adwaita         GTK 标准图标
#    lib/gdk-pixbuf-2.0/*/loaders 图片加载器（png/jpeg/svg/webp/...）+ loaders.cache
#                                （gdk-pixbuf 按 DLL 所在目录找缓存，缓存里是相对路径，
#                                  实测换 CWD 也能加载）
#
#  注意：**不用**带 etc/fonts。Windows 上 GTK 走 PangoCairoWin32FontMap
#  （GDI/DirectWrite），字体直接从系统拿，跟 fontconfig 无关。
# ---------------------------------------------------------------------------

if(NOT WIN32)
    return()
endif()

pkg_get_variable(FOLLOW_MINGW_PREFIX  gtk4           prefix)
pkg_get_variable(FOLLOW_PIXBUF_BINDIR gdk-pixbuf-2.0 gdk_pixbuf_binarydir)
pkg_get_variable(FOLLOW_PIXBUF_BVER   gdk-pixbuf-2.0 gdk_pixbuf_binary_version)

if(NOT FOLLOW_MINGW_PREFIX OR NOT FOLLOW_PIXBUF_BINDIR)
    message(FATAL_ERROR "取不到 MSYS2 前缀 / gdk-pixbuf 目录，pkg-config 正常吗？")
endif()

set(FOLLOW_MINGW_BIN     "${FOLLOW_MINGW_PREFIX}/bin")
set(FOLLOW_PIXBUF_RELDIR "lib/gdk-pixbuf-2.0/${FOLLOW_PIXBUF_BVER}")
set(FOLLOW_RUNTIME_DIR   "${CMAKE_BINARY_DIR}/runtime")

# ------------------------------------------------------------ 图片加载器 ----
file(GLOB FOLLOW_PIXBUF_LOADERS "${FOLLOW_PIXBUF_BINDIR}/loaders/*.dll")
list(LENGTH FOLLOW_PIXBUF_LOADERS _n)
if(_n EQUAL 0)
    message(FATAL_ERROR "找不到 gdk-pixbuf 加载器：${FOLLOW_PIXBUF_BINDIR}/loaders")
endif()

install(DIRECTORY "${FOLLOW_PIXBUF_BINDIR}/loaders"
        DESTINATION "${FOLLOW_PIXBUF_RELDIR}"
        FILES_MATCHING PATTERN "*.dll")

# loaders.cache：query-loaders 会把传入的绝对路径原样写进缓存，这里统一改写成
# 相对 exe 的 lib/gdk-pixbuf-2.0/<ver>/loaders/<name>.dll（配合上面的 chdir）。
find_program(GDK_PIXBUF_QUERY_LOADERS NAMES gdk-pixbuf-query-loaders REQUIRED)

execute_process(
    COMMAND "${GDK_PIXBUF_QUERY_LOADERS}" ${FOLLOW_PIXBUF_LOADERS}
    OUTPUT_VARIABLE _raw
    RESULT_VARIABLE _rc
    ERROR_VARIABLE  _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "gdk-pixbuf-query-loaders 失败: ${_err}")
endif()

string(REGEX REPLACE
       "\"[^\"]*[\\\\/]([^\\\\/\"]+\\.dll)\""
       "\"${FOLLOW_PIXBUF_RELDIR}/loaders/\\1\""
       FOLLOW_PIXBUF_CACHE "${_raw}")

if(FOLLOW_PIXBUF_CACHE MATCHES "\"[A-Za-z]:")
    message(FATAL_ERROR "loaders.cache 里还有绝对路径，检查改写的正则")
endif()

set(FOLLOW_PIXBUF_CACHE_FILE "${CMAKE_BINARY_DIR}/pixbuf-loaders.cache")
file(WRITE "${FOLLOW_PIXBUF_CACHE_FILE}" "${FOLLOW_PIXBUF_CACHE}")
install(FILES "${FOLLOW_PIXBUF_CACHE_FILE}" DESTINATION "${FOLLOW_PIXBUF_RELDIR}" RENAME loaders.cache)

# ------------------------------------------------------------ CA 证书 ------
# libcurl 编译时把 CA 路径写死成 MSYS2 的安装位置，包里得带一份；
# src/net/src/http.c: http_use_bundled_ca() 启动时把它指给 curl。
install(DIRECTORY "${FOLLOW_MINGW_PREFIX}/etc/ssl/certs" DESTINATION etc/ssl
        FILES_MATCHING PATTERN "*.crt")
install(FILES "${FOLLOW_MINGW_PREFIX}/etc/ssl/cert.pem" DESTINATION etc/ssl)

# ------------------------------------------------------- GSettings schema ----
find_program(GLIB_COMPILE_SCHEMAS NAMES glib-compile-schemas REQUIRED)

set(_schema_src "${FOLLOW_MINGW_PREFIX}/share/glib-2.0/schemas")
set(_schema_out "${FOLLOW_RUNTIME_DIR}/share/glib-2.0/schemas")

file(GLOB _schema_files "${_schema_src}/*.xml")
if(NOT _schema_files)
    message(FATAL_ERROR "找不到 GSettings schema：${_schema_src}")
endif()

file(MAKE_DIRECTORY "${_schema_out}")
file(COPY ${_schema_files} DESTINATION "${_schema_out}")
execute_process(COMMAND "${GLIB_COMPILE_SCHEMAS}" --targetdir "${_schema_out}" "${_schema_out}"
                RESULT_VARIABLE _rc ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(WARNING "glib-compile-schemas 失败（沿用 MSYS2 的 gschemas.compiled）: ${_err}")
    file(COPY "${_schema_src}/gschemas.compiled" DESTINATION "${_schema_out}")
endif()

install(DIRECTORY "${_schema_out}" DESTINATION share/glib-2.0)

# --------------------------------------------------------------- 图标主题 ---
foreach(_theme Adwaita hicolor)
    if(EXISTS "${FOLLOW_MINGW_PREFIX}/share/icons/${_theme}")
        install(DIRECTORY "${FOLLOW_MINGW_PREFIX}/share/icons/${_theme}"
                DESTINATION share/icons)
    endif()
endforeach()

# ------------------------------------------------------------------ DLL ----
# GET_RUNTIME_DEPENDENCIES 只能在 install 阶段跑（那时才拿得到最终 exe 路径），
# 具体逻辑放 cmake/InstallDlls.cmake，这里把需要的变量递过去。
install(CODE "
    set(FOLLOW_EXE                   \"$<TARGET_FILE:follow>\")
    set(FOLLOW_MINGW_BIN             \"${FOLLOW_MINGW_BIN}\")
    set(FOLLOW_PIXBUF_BINDIR         \"${FOLLOW_PIXBUF_BINDIR}\")
    set(FOLLOW_PIXBUF_RELDIR         \"${FOLLOW_PIXBUF_RELDIR}\")
    set(FOLLOW_INSTALL_PREFIX        \"\${CMAKE_INSTALL_PREFIX}\")
    include(\"${CMAKE_CURRENT_LIST_DIR}/InstallDlls.cmake\")
")
