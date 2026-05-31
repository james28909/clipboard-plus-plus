cmake_minimum_required(VERSION 3.20)
project(clipboardpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Third-party paths ─────────────────────────────────────────────────────────
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/third_party/imgui")
set(NLOHMANN_DIR "${CMAKE_SOURCE_DIR}/third_party/nlohmann")

# ── ImGui (vendored docking branch) ──────────────────────────────────────────
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

# ── App sources ───────────────────────────────────────────────────────────────
set(APP_SOURCES
    src/main.cpp
    src/app/Application.cpp
    src/app/TrayIcon.cpp
    src/ui/Renderer.cpp
    src/ui/MainWindow.cpp
    src/ui/PopupWindow.cpp
    # Milestone 4 — hotkeys
    src/hotkeys/HotkeyManager.cpp
    # Milestone 2 — clipboard core
    src/clipboard/ClipboardItem.cpp
    src/clipboard/ClipboardHistory.cpp
    src/clipboard/ClipboardMonitor.cpp
    src/clipboard/ContentDetector.cpp
)

# ── Executable ────────────────────────────────────────────────────────────────
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

# ── Static runtime (no MSVC redist needed) ────────────────────────────────────
set_property(TARGET clipboardpp PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ── Windows Unicode + lean headers ───────────────────────────────────────────
target_compile_definitions(clipboardpp PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

# ── Compiler options ──────────────────────────────────────────────────────────
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
    # Suppress the console window in release
    set_target_properties(clipboardpp PROPERTIES
        LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS"
    )
endif()
