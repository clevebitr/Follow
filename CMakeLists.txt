cmake_minimum_required(VERSION 3.25)

# ---------------------------------------------------------------------------
#  Follow
#  开发：
#    cmake --preset mingw64            # 或者 cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#    cmake --build build               # 产物：源码根目录的 main.exe（资源是相对路径）
#    ./main.exe                        # 在源码根目录跑
#
#  打包（NSIS 安装包）：
#    cpack --preset mingw64
#    或者 cmake --install build --prefix /some/dir
# ---------------------------------------------------------------------------

project(Follow
    VERSION      0.1.0
    DESCRIPTION  "把「正在听什么」摆到屏幕上给观众看的极简音乐播放器"
    HOMEPAGE_URL "https://github.com/clevebitr/follow"
    LANGUAGES    C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)          # gnu17：要用到 GNU 扩展（strdup 等）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                 Debug Release RelWithDebInfo MinSizeRel)
endif()

# --------------------------------------------------------------- 依赖 ------
find_package(PkgConfig REQUIRED)

# 与 Makefile 时代保持一致的依赖集合
set(FOLLOW_PKGS
    gtk4
    epoxy
    libqrencode
    libcurl
    json-glib-1.0
    librsvg-2.0
    libavformat
    libavcodec
    libswresample
    libavutil)

pkg_check_modules(FOLLOW_DEPS REQUIRED IMPORTED_TARGET ${FOLLOW_PKGS})

# --------------------------------------------------------------- 选项 ------
set(FOLLOW_EXE_NAME "main" CACHE STRING "可执行文件名（不带 .exe）")
set(FOLLOW_APP_ID "org.github.clevebitr.follow" CACHE STRING "GApplication id")

option(FOLLOW_EXE_IN_SOURCE_DIR
       "可执行文件生成到源码根目录（读 src/ 下的相对路径资源，开发时 ./main.exe）" ON)
option(FOLLOW_WIN_GUI
       "Windows 下用 GUI 子系统（不弹控制台；调试日志会看不到）" OFF)
option(FOLLOW_INSTALL_ASSETS
       "安装/打包时带上 src/assets（自己的音频、封面）" OFF)
option(FOLLOW_BUILD_MCP
       "构建 crates/gtk-mcp-agent-capi 调试用 MCP（需要 cargo）" OFF)

# ------------------------------------------------------------- 可执行文件 ---
file(GLOB_RECURSE FOLLOW_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/*.c")

add_executable(follow ${FOLLOW_SOURCES})

target_include_directories(follow PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src/core"
    "${PROJECT_SOURCE_DIR}/src/ui")

# src/models/miniaudio.h 当系统头文件引入：不报它自己的警告
target_include_directories(follow SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/src/models")

target_link_libraries(follow PRIVATE PkgConfig::FOLLOW_DEPS)

target_compile_definitions(follow PRIVATE FOLLOW_APP_ID="${FOLLOW_APP_ID}")
target_compile_options(follow PRIVATE -Wall -Wextra)

# miniaudio 里的原子操作在 -O2 + 内联时会被误报成 stringop-overflow
set_source_files_properties("${PROJECT_SOURCE_DIR}/src/core/media.c"
    PROPERTIES COMPILE_OPTIONS "-Wno-stringop-overflow")

set_target_properties(follow PROPERTIES OUTPUT_NAME "${FOLLOW_EXE_NAME}")

if(WIN32)
    # Windows 11：去掉 DWM 默认的 1px 边框 / 圆角
    target_link_libraries(follow PRIVATE dwmapi)
    set_target_properties(follow PROPERTIES WIN32_EXECUTABLE ${FOLLOW_WIN_GUI})
endif()

if(FOLLOW_EXE_IN_SOURCE_DIR)
    # 资源路径是相对的，开发时在源码根目录跑。
    set_target_properties(follow PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}")
    foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER "${cfg}" _CFG)
        set_target_properties(follow PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${_CFG} "${PROJECT_SOURCE_DIR}")
    endforeach()
endif()

# 安装/打包
install(TARGETS follow RUNTIME DESTINATION .)

# 程序资源：main.c 按相对路径找 src/style/style.css、src/config.ini、src/icons/*.svg
install(DIRECTORY "${PROJECT_SOURCE_DIR}/src/style"
                  "${PROJECT_SOURCE_DIR}/src/icons"
        DESTINATION src)
install(FILES "${PROJECT_SOURCE_DIR}/src/config.ini" DESTINATION src)

if(FOLLOW_INSTALL_ASSETS AND EXISTS "${PROJECT_SOURCE_DIR}/src/assets")
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/src/assets" DESTINATION src)
endif()

install(FILES "${PROJECT_SOURCE_DIR}/README.md" DESTINATION . OPTIONAL)

# GTK / FFmpeg 的运行时数据与 DLL（Windows）
include(cmake/FollowRuntime.cmake)

# 调试用 MCP（Rust）：cmake --build build --target mcp
if(FOLLOW_BUILD_MCP)
    add_custom_target(mcp
        COMMAND cargo build --release
                --manifest-path "${PROJECT_SOURCE_DIR}/crates/gtk-mcp-agent-capi/Cargo.toml"
        COMMENT "构建 gtk-mcp-agent（stdio MCP 调试服务）"
        VERBATIM)

    add_dependencies(mcp follow)
    install(PROGRAMS
            "${PROJECT_SOURCE_DIR}/crates/gtk-mcp-agent-capi/target/release/gtk-mcp-agent.exe"
            DESTINATION . OPTIONAL)
endif()

# CPack：NSIS 安装包
include(cmake/FollowPackaging.cmake)
