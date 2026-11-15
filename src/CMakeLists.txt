# ---------------------------------------------------------------------------
#  Follow · 模块化源码树
#
#  每个模块一个独立静态库 + 一个公开 umbrella 头文件，只允许单向依赖：
#
#      app ──► ui ──► music ──► net ──► core
#
#  单独编译某个模块（不链接可执行文件）：
#      cmake --build build --target follow_core
#      cmake --build build --target follow_music
#
#  公开头文件在 <module>/include/ 下，随 target_link_libraries 自动传播；
#  模块内部实现放在 <module>/src/，其它模块不应该 include。
# ---------------------------------------------------------------------------

# 所有模块共用的第三方依赖（GTK / curl / json-glib / sqlite3 / FFmpeg ...）
add_library(follow_deps INTERFACE)
target_link_libraries(follow_deps INTERFACE PkgConfig::FOLLOW_DEPS)

# follow_module(<name> SOURCES <files>... DEPENDS <modules>...)
function(follow_module name)
    cmake_parse_arguments(MOD "" "" "SOURCES;DEPENDS" ${ARGN})

    add_library(follow_${name} STATIC ${MOD_SOURCES})
    add_library(follow::${name} ALIAS follow_${name})

    target_include_directories(follow_${name}
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/${name}/include")

    foreach(dep IN LISTS MOD_DEPENDS)
        target_link_libraries(follow_${name} PUBLIC follow::${dep})
    endforeach()

    # 第三方依赖只传播 include / 编译选项；静态库不重复链接
    target_link_libraries(follow_${name} PUBLIC follow_deps)

    target_compile_options(follow_${name} PRIVATE -Wall -Wextra)
    set_target_properties(follow_${name} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endfunction()

# ------------------------------------------------------------------ core ---
follow_module(core
    SOURCES
        core/src/cache.c
        core/src/config.c
        core/src/keymap.c
        core/src/palette.c
        core/src/platform.c
        core/src/settings.c
        core/src/stats.c
        core/src/texture.c)

# ------------------------------------------------------------------- net ---
follow_module(net
    SOURCES
        net/src/http.c
        net/src/httpstream.c
        net/src/source.c
        net/src/resolvecache.c
        net/src/bilibili.c
        net/src/youtube.c
        net/src/musicbrainz.c
    DEPENDS core)

# ----------------------------------------------------------------- music ---
follow_module(music
    SOURCES
        music/src/media.c
        music/src/lyrics.c
        music/src/lyricsrc.c
        music/src/metadb.c
        music/src/metadata.c
    DEPENDS net core)

# src/models/miniaudio.h 当系统头文件引入：不报它自己的警告
# media.h 本身就 include "miniaudio.h"，所以这条 include 必须向下游传播
target_include_directories(follow_music SYSTEM PUBLIC
    "${PROJECT_SOURCE_DIR}/src/models")

# miniaudio 里的原子操作在 -O2 + 内联时会被误报成 stringop-overflow
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/music/src/media.c"
    PROPERTIES COMPILE_OPTIONS "-Wno-stringop-overflow")

# -------------------------------------------------------------------- ui ---
follow_module(ui
    SOURCES
        ui/src/backdrop.c
        ui/src/backdropgl.c
        ui/src/cmdbox.c
        ui/src/cover.c
        ui/src/covercache.c
        ui/src/details.c
        ui/src/fx.c
        ui/src/glutil.c
        ui/src/help.c
        ui/src/layout.c
        ui/src/lyricgl.c
        ui/src/lyricview.c
        ui/src/perf.c
        ui/src/player.c
        ui/src/playlist.c
        ui/src/qrcard.c
        ui/src/stage.c
        ui/src/theme.c
        ui/src/uidbg.c
        ui/src/viz.c
        ui/src/wallgl.c
    DEPENDS music net core)

# ------------------------------------------------------------- app / exe ---
add_executable(follow app/main.c)

add_library(follow_api INTERFACE)
target_include_directories(follow_api INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(follow PRIVATE follow_api follow::ui follow::music
                                      follow::net follow::core)
target_compile_options(follow PRIVATE -Wall -Wextra)
