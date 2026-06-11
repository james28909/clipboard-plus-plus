cmake_minimum_required(VERSION 3.20)
project(clipboardpp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(CLIPBOARDPP_RESTART_AFTER_BUILD "Stop running clipboardpp.exe before build and launch the new build afterward" ON)

# -- Third-party paths ---------------------------------------------------------
set(IMGUI_DIR   "${CMAKE_SOURCE_DIR}/third_party/imgui")
set(NLOHMANN_DIR "${CMAKE_SOURCE_DIR}/third_party/nlohmann")
set(SQLITE_DIR  "${CMAKE_SOURCE_DIR}/third_party/sqlite")

# -- ImGui (vendored docking branch) ------------------------------------------
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

# -- App sources ---------------------------------------------------------------
set(APP_SOURCES
    src/main.cpp
    src/cli/CLI.cpp
    src/ipc/IpcClient.cpp
    src/util/Win32Util.cpp
    src/app/Application.cpp
    src/app/ConfigStore.cpp
    src/app/TrayIcon.cpp
    src/ui/Appearance.cpp
    src/ui/MainWindow.cpp
    src/ui/PopupWindow.cpp
    src/ui/TrayPopupWindow.cpp
    src/ui/DebugWindow.cpp
    # Milestone 4 - hotkeys
    src/hotkeys/HotkeyManager.cpp
    # Milestone 2 - clipboard core
    src/clipboard/ClipboardItem.cpp
    src/clipboard/ClipboardHistory.cpp
    src/clipboard/ClipboardHistoryStore.cpp
    src/clipboard/ClipboardMonitor.cpp
    src/clipboard/ContentDetector.cpp
    src/clipboard/ImageStore.cpp
    ${SQLITE_DIR}/sqlite3.c
)

set(PORTABLE_CORE_SOURCES
    src/app/ConfigStore.cpp
    src/ui/Appearance.cpp
    src/hotkeys/HotkeyManager.cpp
    src/clipboard/ClipboardItem.cpp
    src/clipboard/ClipboardHistory.cpp
    src/clipboard/ClipboardHistoryStore.cpp
    src/clipboard/ContentDetector.cpp
)

# -- Windows executable ---------------------------------------------------------
if(WIN32)
    add_executable(clipboardpp WIN32
        ${APP_SOURCES}
        ${IMGUI_SOURCES}
        resources/app.rc
    )

    target_include_directories(clipboardpp PRIVATE
        src
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${NLOHMANN_DIR}
        ${SQLITE_DIR}
    )

    target_link_libraries(clipboardpp PRIVATE
        d3d11.lib
        dxgi.lib
        d3dcompiler.lib
        dwmapi.lib
        comctl32.lib
        shell32.lib
        shlwapi.lib
        user32.lib
        gdi32.lib
        psapi.lib
    )

    if(CLIPBOARDPP_RESTART_AFTER_BUILD)
        add_custom_command(TARGET clipboardpp PRE_BUILD
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
                    "Stop-Process -Name clipboardpp -Force -ErrorAction SilentlyContinue; exit 0"
            COMMENT "Stopping running clipboardpp.exe before build"
            VERBATIM
        )

        add_custom_command(TARGET clipboardpp POST_BUILD
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
                    "Start-Process -FilePath '$<TARGET_FILE:clipboardpp>' -WorkingDirectory '$<TARGET_FILE_DIR:clipboardpp>'"
            COMMENT "Launching clipboardpp.exe after build"
            VERBATIM
        )
    endif()

    # -- Static runtime (no MSVC redist needed) --------------------------------
    set_property(TARGET clipboardpp PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    # -- Windows Unicode + lean headers ----------------------------------------
    target_compile_definitions(clipboardpp PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        SQLITE_DEFAULT_FOREIGN_KEYS=1
        SQLITE_OMIT_LOAD_EXTENSION
    )
endif()

if(UNIX AND NOT APPLE)
    add_executable(clipboardpp_linux_probe
        src/linux/LinuxProbe.cpp
        ${PORTABLE_CORE_SOURCES}
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
    )

    target_include_directories(clipboardpp_linux_probe PRIVATE
        src
        ${IMGUI_DIR}
        ${NLOHMANN_DIR}
    )

    find_package(OpenGL QUIET)
    find_package(glfw3 QUIET)
    if(OpenGL_FOUND AND glfw3_FOUND)
        add_executable(clipboardpp_linux_imgui_probe
            src/linux/ImguiGlfwProbe.cpp
            ${PORTABLE_CORE_SOURCES}
            ${IMGUI_DIR}/imgui.cpp
            ${IMGUI_DIR}/imgui_draw.cpp
            ${IMGUI_DIR}/imgui_tables.cpp
            ${IMGUI_DIR}/imgui_widgets.cpp
            ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
            ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
        )

        target_include_directories(clipboardpp_linux_imgui_probe PRIVATE
            src
            ${IMGUI_DIR}
            ${IMGUI_DIR}/backends
            ${NLOHMANN_DIR}
        )

        target_link_libraries(clipboardpp_linux_imgui_probe PRIVATE
            glfw
            OpenGL::GL
        )
    endif()
endif()

# -- Compiler options ----------------------------------------------------------
if(MSVC)
    target_compile_options(clipboardpp PRIVATE
        /W3
        /utf-8
        /wd4996   # silence deprecated POSIX name warnings
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
    )
    target_link_options(clipboardpp PRIVATE
        $<$<CONFIG:Release>:/LTCG>
    )
    target_link_options(clipboardpp PRIVATE /MANIFEST:NO)
    target_link_options(clipboardpp PRIVATE /SUBSYSTEM:CONSOLE)
endif()
