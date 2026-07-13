cmake_minimum_required(VERSION 3.20)
project(sqlite_editor VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_DIR   "${CMAKE_SOURCE_DIR}/../third_party/imgui")
set(SQLITE_DIR  "${CMAKE_SOURCE_DIR}/../third_party/sqlite")

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

add_executable(sqlite_editor WIN32
    src/main.cpp
    src/DbViewer.cpp
    src/ClipboardImageBlob.cpp
    ../src/security/DpapiProtection.cpp
    ${SQLITE_DIR}/sqlite3.c
    ${IMGUI_SOURCES}
    res/sqlite_editor.rc
)

target_include_directories(sqlite_editor PRIVATE
    src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${SQLITE_DIR}
)

target_link_libraries(sqlite_editor PRIVATE
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
    dwmapi.lib
    comctl32.lib
    comdlg32.lib
    shlwapi.lib
    crypt32.lib
)

target_compile_definitions(sqlite_editor PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

set_property(TARGET sqlite_editor PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(WIN32)
    add_executable(sqlite_editor_image_tests
        tests/ClipboardImageBlobTests.cpp
        src/ClipboardImageBlob.cpp
        ../src/security/DpapiProtection.cpp
    )
    target_include_directories(sqlite_editor_image_tests PRIVATE src)
    target_link_libraries(sqlite_editor_image_tests PRIVATE crypt32.lib)
    target_compile_definitions(sqlite_editor_image_tests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    set_property(TARGET sqlite_editor_image_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_compile_options(sqlite_editor PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W3 /permissive- /Zc:__cplusplus>)

# Manifest is embedded via the .rc file; tell the linker not to generate a second one
if(MSVC)
    target_link_options(sqlite_editor PRIVATE /MANIFEST:NO)
endif()
