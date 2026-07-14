# JSON Viewer

A lightweight standalone JSON file viewer for Windows, built with C++17, Dear ImGui, and DirectX 11. Part of the [Clipboard++](../README.md) toolset.

## Features

- **Colored tree view** - keys in blue-gray, strings in green, numbers in orange, booleans in cyan, nulls in purple; objects and arrays show child counts
- **Case-insensitive search** - type in the toolbar to highlight matching keys and values; parent nodes auto-expand to reveal matches
- **Expand All / Collapse All** - toolbar buttons and View menu
- **Raw JSON panel** - toggleable bottom panel showing the original file text in monospace (Consolas), fully selectable and copyable
- **Right-click leaf nodes** - context menu to copy the key or value to the clipboard
- **Recent files** - up to 10 recently opened files, persisted in `%APPDATA%\json_viewer_recents.txt`
- **Drag and drop** - drop any `.json` file onto the window to open it
- **Dark theme** - matches the Clipboard++ visual style (Dear ImGui + DWM dark title bar)
- **Keyboard shortcuts** - `Ctrl+O` open, `F5` reload

## Build

Requires Visual Studio 2022 and CMake 3.20+.

```powershell
cd jsonviewer
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output: `jsonviewer/build/Release/json_viewer.exe`

## Usage

Launch the exe directly, pass a file path as an argument, or use the right-click context menu:

```powershell
.\json_viewer.exe
.\json_viewer.exe C:\path\to\file.json
```

You can also drag and drop a `.json` file onto the window after launch.

## Right-click Context Menu

To add **Open with JSON Viewer** to the Windows Explorer right-click menu for `.json` files, double-click:

```
install-context-menu.reg
```

This registers the entry under `HKCU` (current user only, no admin required). The path in the `.reg` file points to `build\Release\json_viewer.exe` - update it if you move the executable.

## Sample File

`sample.json` in this folder is a Clipboard++ project configuration that exercises all JSON value types: nested objects, arrays, strings, numbers, booleans, and nulls. Open it to see the tree view and try the search bar.

## Tips

- **Large files** - containers with more than 500 children show a truncated view with a "... N more" indicator
- **Copy a value** - right-click any leaf node, or open the Raw JSON panel and select text directly
- **Search** - the search box filters by key name and string/number value simultaneously; press the **x** button or clear the field to reset
- **Raw panel** - toggle with the **Raw** button in the toolbar or via View > Raw JSON Panel; resize by dragging the horizontal splitter
