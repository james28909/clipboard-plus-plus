# JSON Viewer

A small standalone GUI for opening and inspecting `.json` files.

## Run

From this directory:

```powershell
python .\json_viewer.py
```

You can also pass a file path:

```powershell
python .\json_viewer.py C:\path\to\file.json
```

## Features

- Open `.json` files from a native file picker.
- View JSON as an expandable tree.
- Select a tree key/index to show that selected JSON value on the right.
- Jump back to the full document view.
- Copy the selected JSON path or selected JSON value.
- Search keys and values in the tree.
- See parse errors with file location details when JSON is invalid.

## Right-click Menu

To add `Open with JSON Viewer` to the Windows right-click menu for `.json` files,
double-click:

```text
install-json-viewer-context-menu.reg
```

To remove it later, double-click:

```text
uninstall-json-viewer-context-menu.reg
```
