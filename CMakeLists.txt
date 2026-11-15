cmake_minimum_required(VERSION 3.25)

# Follow
# 开发：cmake --preset mingw64 && cmake --build build
# 打包：cpack --preset mingw64

set(FOLLOW_VERSION "0.1.1" CACHE STRING "项目版本（release 时由 tag 覆盖）")

project(Follow
    VERSION      ${FOLLOW_VERSION}
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

# 依赖
find_package(PkgConfig REQUIRED)

set(FOLLOW_PKGS
    gtk4
    epoxy
    libqrencode
    libcurl
    json-glib-1.0
    librsvg-2.0
    sqlite3
    libavformat
    libavcodec
    libswresample
    libavutil)

if(UNIX AND NOT APPLE)
    list(APPEND FOLLOW_PKGS libsecret-1)
endif()

pkg_check_modules(FOLLOW_DEPS REQUIRED IMPORTED_TARGET ${FOLLOW_PKGS})

# 选项
set(FOLLOW_EXE_NAME "follow" CACHE STRING "可执行文件名（不带 .exe）")
set(FOLLOW_APP_ID "org.github.clevebitr.follow" CACHE STRING "GApplication id")

option(FOLLOW_EXE_IN_SOURCE_DIR
       "可执行文件生成到源码根目录（读 src/ 下的相对路径资源，开发时 ./follow.exe）" ON)
option(FOLLOW_WIN_GUI
       "Windows 使用 GUI 子系统（默认不显示控制台；调试时用 OFF）" ON)
option(FOLLOW_INSTALL_ASSETS
       "安装/打包时带上 src/assets（自己的音频、封面）" OFF)
option(FOLLOW_BUILD_MCP
       "构建 crates/gtk-mcp-agent-capi 调试用 MCP（需要 cargo）" OFF)

# 模块与可执行文件
add_subdirectory(src)

target_compile_definitions(follow PRIVATE FOLLOW_APP_ID="${FOLLOW_APP_ID}")
target_compile_definitions(follow_ui PRIVATE FOLLOW_APP_ID="${FOLLOW_APP_ID}")

if(UNIX AND NOT APPLE)
    target_link_libraries(follow PRIVATE m dl pthread)
endif()

set_target_properties(follow PROPERTIES OUTPUT_NAME "${FOLLOW_EXE_NAME}")

if(WIN32)
    enable_language(RC)
    target_sources(follow PRIVATE "${PROJECT_SOURCE_DIR}/src/icons/logo.rc")
    target_include_directories(follow PRIVATE "${PROJECT_SOURCE_DIR}/src/icons")

    # Windows 11：去掉 DWM 默认的 1px 边框 / 圆角
    target_link_libraries(follow PRIVATE dwmapi advapi32)
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

# 安装 / 打包
install(TARGETS follow RUNTIME DESTINATION .)

install(DIRECTORY "${PROJECT_SOURCE_DIR}/src/style"
                  "${PROJECT_SOURCE_DIR}/src/icons"
        DESTINATION src)
install(FILES "${PROJECT_SOURCE_DIR}/src/config.ini" DESTINATION src)

if(FOLLOW_INSTALL_ASSETS AND EXISTS "${PROJECT_SOURCE_DIR}/src/assets")
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/src/assets" DESTINATION src)
endif()

install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION .)
install(FILES "${PROJECT_SOURCE_DIR}/README.md" DESTINATION . OPTIONAL)

if(UNIX AND NOT APPLE)
    configure_file("${PROJECT_SOURCE_DIR}/cmake/follow.desktop.in"
                   "${CMAKE_BINARY_DIR}/follow.desktop" @ONLY)
    install(FILES "${CMAKE_BINARY_DIR}/follow.desktop"
            DESTINATION share/applications
            RENAME "${FOLLOW_APP_ID}.desktop")
    install(FILES "${PROJECT_SOURCE_DIR}/src/icons/logo-mark.svg"
            DESTINATION share/icons/hicolor/scalable/apps
            RENAME "${FOLLOW_APP_ID}.svg")
endif()

# 运行时 DLL（Windows）
include(cmake/FollowRuntime.cmake)

# 可选的调试 MCP（Rust）
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

include(cmake/FollowPackaging.cmake)
