cmake_minimum_required(VERSION 3.20)
project(clipboardpp_ide VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_DIR    "${CMAKE_SOURCE_DIR}/../third_party/imgui")
set(NLOHMANN_DIR "${CMAKE_SOURCE_DIR}/../third_party/nlohmann")
set(SCINTILLA_DIR "${CMAKE_SOURCE_DIR}/../third_party/scintilla")

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

add_executable(clipboardpp_ide WIN32
    src/main.cpp
    src/IdeApp.cpp
    ${IMGUI_SOURCES}
    res/clipboardpp_ide.rc
)

target_include_directories(clipboardpp_ide PRIVATE
    src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${NLOHMANN_DIR}
    ${SCINTILLA_DIR}/include
)

target_link_libraries(clipboardpp_ide PRIVATE
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
    dwmapi.lib
    comctl32.lib
    comdlg32.lib
    shell32.lib
    shlwapi.lib
)

target_compile_definitions(clipboardpp_ide PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

set_property(TARGET clipboardpp_ide PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_compile_options(clipboardpp_ide PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W3 /permissive- /Zc:__cplusplus /utf-8>)

if(MSVC)
    target_link_options(clipboardpp_ide PRIVATE /MANIFEST:NO)
endif()

add_custom_target(scintilla_win32
    COMMAND nmake -f scintilla.mak QUIET=1
    WORKING_DIRECTORY ${SCINTILLA_DIR}/win32
    COMMENT "Building Scintilla.dll"
)

add_dependencies(clipboardpp_ide scintilla_win32)

add_custom_command(TARGET clipboardpp_ide POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SCINTILLA_DIR}/bin/Scintilla.dll"
            "$<TARGET_FILE_DIR:clipboardpp_ide>/Scintilla.dll"
    COMMENT "Copying Scintilla.dll beside clipboardpp_ide.exe"
)
