# SQLite Editor

A lightweight standalone SQLite database browser and editor for Windows, built with C++17, Dear ImGui, and DirectX 11. Part of the [Clipboard++](../README.md) toolset.

## Features

- **Table browser** — left panel lists all tables with row counts; click to load
- **Data grid** — sortable columns, row selection, inline cell editing with commit/rollback
- **SQL query panel** — write and run arbitrary SQL; results shown in a separate grid
- **BLOB image preview** — automatically detects image columns and displays decoded images (PNG, JPEG, BMP, GIF, TIFF, raw DIB) in a resizable side panel with save-to-file support; Clipboard++ image BLOBs are decrypted in memory with current-user DPAPI
- **Recent files** — up to 10 recently opened databases, persisted in `%APPDATA%\sqlite_editor_recents.txt`
- **Drag and drop** — drop a `.db` or `.sqlite` file onto the window to open it
- **Dark theme** — matches the Clipboard++ visual style (Dear ImGui + DWM dark title bar)
- **Keyboard shortcuts** — `Ctrl+O` open, `F5` refresh/reload, `Delete` delete selected row

## Build

Requires Visual Studio 2022 and CMake 3.20+.

```powershell
cd dbviewer
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output: `dbviewer/build/Release/sqlite_editor.exe`

## Usage

Launch the exe directly, or pass a database path as the first argument:

```powershell
.\sqlite_editor.exe
.\sqlite_editor.exe C:\path\to\database.db
```

You can also drag and drop a `.db` file onto the window after launch.

## Sample Database

`sample.db` in this folder demonstrates the BLOB image preview feature. It contains two tables:

| Table         | Contents |
|---------------|----------|
| `screenshots` | 10 Clipboard++ UI screenshots stored as PNG BLOBs, with filename, label, description, and pixel dimensions |
| `tags`        | Tag rows linked to each screenshot by foreign key |

Open `sample.db` in the editor and click any row in `screenshots` to see the image preview panel appear on the right.

## Tips

- **Resize panels** — drag the vertical splitter between the table list and data grid, or the horizontal splitter between the grid and the image preview
- **Edit a cell** — double-click any cell; press Enter to commit or Escape to cancel
- **Run SQL** — type in the SQL panel at the bottom and press `Ctrl+Enter` or click **Run**
- **Image preview** — only appears when the selected table has a BLOB column; the preview shows the first BLOB column in the row
