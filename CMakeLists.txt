cmake_minimum_required(VERSION 3.20)
project(clipboardpp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(CLIPBOARDPP_RESTART_AFTER_BUILD "Stop running clipboardpp.exe before build and launch the new build afterward" ON)

# -- Third-party paths ---------------------------------------------------------
set(IMGUI_DIR   "${CMAKE_SOURCE_DIR}/third_party/imgui")
set(NLOHMANN_DIR "${CMAKE_SOURCE_DIR}/third_party/nlohmann")
set(SQLITE_DIR  "${CMAKE_SOURCE_DIR}/third_party/sqlite")
set(PCRE2_DIR   "${CMAKE_SOURCE_DIR}/third_party/pcre2")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_8 ON CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_16 OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_32 OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PCRE2_SUPPORT_JIT ON CACHE BOOL "" FORCE)
set(PCRE2_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
add_subdirectory(${PCRE2_DIR} EXCLUDE_FROM_ALL)
if(TARGET pcre2-8-static)
    set_property(TARGET pcre2-8-static PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# -- ImGui (vendored docking branch) ------------------------------------------
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

# -- App sources ---------------------------------------------------------------
set(APP_SOURCES
    src/main.cpp
    src/android/AndroidDeviceClient.cpp
    src/android/AndroidIntegration.cpp
    src/android/AndroidSyncServer.cpp
    src/cli/CLI.cpp
    src/cli/CliStorage.cpp
    src/cli/HistoryCli.cpp
    src/cli/VaultCli.cpp
    src/ipc/IpcClient.cpp
    src/util/Win32Util.cpp
    src/app/Application.cpp
    src/app/ConfigStore.cpp
    src/app/IconPatcher.cpp
    src/app/StartupRegistration.cpp
    src/app/StartupProfiler.cpp
    src/app/TrayIcon.cpp
    src/filters/CustomFilter.cpp
    src/actions/CustomAction.cpp
    src/transforms/RegexTransform.cpp
    src/templates/PasteTemplate.cpp
    src/formatting/StructuredFormatter.cpp
    src/diff/TextDiff.cpp
    src/security/DpapiProtection.cpp
    src/security/EncryptedSqliteVfs.cpp
    src/security/BackupRestore.cpp
    src/security/StatePackage.cpp
    src/support/SupportBundle.cpp
    src/ui/Appearance.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindowGeneral.cpp
    src/ui/MainWindowActions.cpp
    src/ui/MainWindowHotkeys.cpp
    src/ui/MainWindowAppearance.cpp
    src/ui/MainWindowHistory.cpp
    src/ui/MainWindowFilters.cpp
    src/ui/MainWindowEditor.cpp
    src/ui/MainWindowImages.cpp
    src/ui/MainWindowAndroid.cpp
    src/ui/MainWindowPrivacy.cpp
    src/ui/MainWindowDeveloper.cpp
    src/ui/MainWindowAbout.cpp
    src/ui/MainWindowSupport.cpp
    src/ui/PopupWindow.cpp
    src/ui/PopupWindowToolbar.cpp
    src/ui/PopupWindowActions.cpp
    src/ui/PopupWindowAndroid.cpp
    src/ui/PopupWindowHistory.cpp
    src/ui/PopupWindowImages.cpp
    src/ui/PopupWindowPaste.cpp
    src/ui/PopupSelectionModel.cpp
    src/ui/GeneratedPaste.cpp
    src/ui/PasteDiagnostics.cpp
    src/ui/TrayPopupWindow.cpp
    src/ui/TextEditorWindow.cpp
    src/ui/ToastWindow.cpp
    src/ui/DebugWindow.cpp
    # Milestone 4 - hotkeys
    src/hotkeys/HotkeyManager.cpp
    # Milestone 2 - clipboard core
    src/clipboard/ClipboardItem.cpp
    src/clipboard/ClipboardHistory.cpp
    src/clipboard/ClipboardDatabase.cpp
    src/clipboard/ClipboardProfileManager.cpp
    src/clipboard/ClipboardHistoryStore.cpp
    src/clipboard/ClipboardMonitor.cpp
    src/clipboard/ContentDetector.cpp
    src/clipboard/ImageStore.cpp
    src/clipboard/ScreenshotTracker.cpp
    ${SQLITE_DIR}/sqlite3.c
)

set(PORTABLE_CORE_SOURCES
    src/app/ConfigStore.cpp
    src/filters/CustomFilter.cpp
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
        ${PCRE2_DIR}/src
    )

    target_link_libraries(clipboardpp PRIVATE
        pcre2-8-static
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
        crypt32.lib
        bcrypt.lib
        ws2_32.lib
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

add_executable(filter_matcher_tests
    tests/filter_matcher_tests.cpp
    src/actions/CustomAction.cpp
    src/util/Win32Util.cpp
    src/filters/CustomFilter.cpp
    src/transforms/RegexTransform.cpp
    src/templates/PasteTemplate.cpp
    src/formatting/StructuredFormatter.cpp
    src/diff/TextDiff.cpp
    src/ui/GeneratedPaste.cpp
    src/app/StartupProfiler.cpp
    src/clipboard/ClipboardItem.cpp
    src/clipboard/ClipboardHistory.cpp
    src/clipboard/ContentDetector.cpp
)

if(WIN32)
    add_executable(support_bundle_tests
        tests/support_bundle_tests.cpp
        src/support/SupportBundle.cpp
        src/util/Win32Util.cpp
    )

    target_include_directories(support_bundle_tests PRIVATE
        src
        ${NLOHMANN_DIR}
    )

    target_link_libraries(support_bundle_tests PRIVATE user32.lib shell32.lib)

    set_property(TARGET support_bundle_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    target_compile_definitions(support_bundle_tests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

target_include_directories(filter_matcher_tests PRIVATE
    src
    ${PCRE2_DIR}/src
    ${NLOHMANN_DIR}
)

target_link_libraries(filter_matcher_tests PRIVATE pcre2-8-static)

if(WIN32)
    target_compile_definitions(filter_matcher_tests PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

set_property(TARGET filter_matcher_tests PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(WIN32)
    add_executable(dpapi_protection_tests
        tests/dpapi_protection_tests.cpp
        src/security/DpapiProtection.cpp
    )
    target_include_directories(dpapi_protection_tests PRIVATE src)
    target_link_libraries(dpapi_protection_tests PRIVATE crypt32.lib)
    target_compile_definitions(dpapi_protection_tests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    set_property(TARGET dpapi_protection_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    add_executable(history_encryption_tests
        tests/history_encryption_tests.cpp
        src/security/DpapiProtection.cpp
        src/util/Win32Util.cpp
        src/clipboard/ClipboardItem.cpp
        src/clipboard/ClipboardHistory.cpp
        src/clipboard/ClipboardHistoryStore.cpp
    )
    target_include_directories(history_encryption_tests PRIVATE
        src
        ${IMGUI_DIR}
        ${NLOHMANN_DIR}
    )
    target_link_libraries(history_encryption_tests PRIVATE crypt32.lib)
    target_compile_definitions(history_encryption_tests PRIVATE
        CLIPBOARDPP_TESTING
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    set_property(TARGET history_encryption_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    add_executable(encrypted_sqlite_vfs_tests
        tests/encrypted_sqlite_vfs_tests.cpp
        src/security/BackupRestore.cpp
        src/security/DpapiProtection.cpp
        src/security/EncryptedSqliteVfs.cpp
        ${SQLITE_DIR}/sqlite3.c
    )
    target_include_directories(encrypted_sqlite_vfs_tests PRIVATE
        src
        ${NLOHMANN_DIR}
        ${SQLITE_DIR}
    )
    target_link_libraries(encrypted_sqlite_vfs_tests PRIVATE
        crypt32.lib
        bcrypt.lib
    )
    target_compile_definitions(encrypted_sqlite_vfs_tests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        SQLITE_DEFAULT_FOREIGN_KEYS=1
        SQLITE_OMIT_LOAD_EXTENSION
    )
    set_property(TARGET encrypted_sqlite_vfs_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    add_executable(clipboard_database_tests
        tests/clipboard_database_tests.cpp
        src/actions/CustomAction.cpp
        src/transforms/RegexTransform.cpp
        src/templates/PasteTemplate.cpp
        src/formatting/StructuredFormatter.cpp
        src/security/DpapiProtection.cpp
        src/security/EncryptedSqliteVfs.cpp
        src/security/StatePackage.cpp
        src/util/Win32Util.cpp
        src/clipboard/ClipboardItem.cpp
        src/clipboard/ClipboardHistory.cpp
        src/clipboard/ClipboardHistoryStore.cpp
        src/clipboard/ClipboardDatabase.cpp
        ${SQLITE_DIR}/sqlite3.c
    )
    target_include_directories(clipboard_database_tests PRIVATE
        src
        ${IMGUI_DIR}
        ${NLOHMANN_DIR}
        ${SQLITE_DIR}
        ${PCRE2_DIR}/src
    )
    target_link_libraries(clipboard_database_tests PRIVATE
        crypt32.lib
        bcrypt.lib
        pcre2-8-static
    )
    target_compile_definitions(clipboard_database_tests PRIVATE
        CLIPBOARDPP_TESTING
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        SQLITE_DEFAULT_FOREIGN_KEYS=1
        SQLITE_OMIT_LOAD_EXTENSION
    )
    set_property(TARGET clipboard_database_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    add_executable(image_encryption_tests
        tests/image_encryption_tests.cpp
        src/security/DpapiProtection.cpp
        src/security/EncryptedSqliteVfs.cpp
        src/clipboard/ImageStore.cpp
        ${SQLITE_DIR}/sqlite3.c
    )
    target_include_directories(image_encryption_tests PRIVATE
        src
        ${IMGUI_DIR}
        ${SQLITE_DIR}
    )
    target_link_libraries(image_encryption_tests PRIVATE
        crypt32.lib
        bcrypt.lib
        d3d11.lib
        gdi32.lib
        gdiplus.lib
        ole32.lib
    )
    target_compile_definitions(image_encryption_tests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        SQLITE_DEFAULT_FOREIGN_KEYS=1
        SQLITE_OMIT_LOAD_EXTENSION
    )
    set_property(TARGET image_encryption_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
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
