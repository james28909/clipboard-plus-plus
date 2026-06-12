cmake_minimum_required(VERSION 3.20)
project(json_viewer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_DIR    "${CMAKE_SOURCE_DIR}/../third_party/imgui")
set(NLOHMANN_DIR "${CMAKE_SOURCE_DIR}/../third_party/nlohmann")

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

add_executable(json_viewer WIN32
    src/main.cpp
    src/JsonViewer.cpp
    ${IMGUI_SOURCES}
    res/json_viewer.rc
)

target_include_directories(json_viewer PRIVATE
    src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${NLOHMANN_DIR}
)

target_link_libraries(json_viewer PRIVATE
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
    dwmapi.lib
    comctl32.lib
    comdlg32.lib
    shlwapi.lib
)

target_compile_definitions(json_viewer PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

set_property(TARGET json_viewer PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_compile_options(json_viewer PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W3 /permissive- /Zc:__cplusplus>)

# Manifest is embedded via the .rc file; tell the linker not to generate a second one
if(MSVC)
    target_link_options(json_viewer PRIVATE /MANIFEST:NO)
endif()
