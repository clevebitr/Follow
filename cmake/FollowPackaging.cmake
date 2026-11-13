# ---------------------------------------------------------------------------
#  CPack：NSIS 安装包
#
#  默认装到 %ProgramFiles64%\Follow，建开始菜单 + 桌面快捷方式，带卸载项。
#  程序只在 %LOCALAPPDATA% 里写缓存和音量设置，配置 src/config.ini 只读没问题；
#  快捷方式的「起绐位置」是 $INSTDIR，所以 src/ 下的相对路径资源能找到。
#
#  用法：
#    cpack --preset mingw64
#    cpack -G NSIS --config build/CPackConfig.cmake
# ---------------------------------------------------------------------------

set(CPACK_PACKAGE_NAME            "Follow")
set(CPACK_PACKAGE_VENDOR          "clevebitr")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "把「正在听什么」摆到屏幕上给观众看的极简音乐播放器")
set(CPACK_PACKAGE_VERSION         "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR   "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR   "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH   "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_FILE_NAME       "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-win64")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Follow")
set(CPACK_PACKAGE_EXECUTABLES     "${FOLLOW_EXE_NAME}" "Follow")

set(CPACK_GENERATOR "NSIS")

# ------------------------------------------------------------------ NSIS ----
set(CPACK_NSIS_DISPLAY_NAME        "Follow")
set(CPACK_NSIS_PACKAGE_NAME        "Follow")
set(CPACK_NSIS_INSTALLED_ICON_NAME "${FOLLOW_EXE_NAME}.exe")
set(CPACK_NSIS_MENU_LINKS          "${FOLLOW_EXE_NAME}.exe" "Follow")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MUI_LANGUAGES       "SimpChinese;English")
set(CPACK_NSIS_INSTALL_ROOT        "$PROGRAMFILES64")

# 桌面快捷方式
set(CPACK_NSIS_CREATE_ICONS_EXTRA
    "CreateShortCut '$DESKTOP\\\\Follow.lnk' '$INSTDIR\\\\${FOLLOW_EXE_NAME}.exe'")
set(CPACK_NSIS_DELETE_ICONS_EXTRA
    "Delete '$DESKTOP\\\\Follow.lnk'")

# 卸载时保留用户数据（%LOCALAPPDATA%\follow 里的缓存 / 音量设置）
set(CPACK_NSIS_DELETE_EXTRA "")

include(CPack)
