# Icons — Build Guide

This folder contains SVG source files for all three project icons and the tooling to render them into multi-resolution `.ico` files suitable for Windows.

## Files

| File | Purpose |
|---|---|
| `clipboardpp.svg` | Clipboard++ main icon — Jugo-style clipboard with theme colors |
| `sqlite_editor.svg` | SQLite Editor (db viewer) icon |
| `json_viewer.svg` | JSON Viewer icon |
| `render_svg.js` | Node.js script — renders one SVG to 7 PNG sizes |
| `make_ico.py` | Python script — assembles 7 PNGs into a multi-resolution ICO |
| `package.json` | npm config for `@resvg/resvg-js` (Rust-based SVG renderer) |

## Prerequisites

- **Node.js** 18+ — [nodejs.org](https://nodejs.org)
- **Python 3** — [python.org](https://www.python.org)
- **npm packages** — install once from this folder:

```powershell
cd icons
npm install
```

This installs `@resvg/resvg-js` — a Rust-based SVG renderer with no system dependencies. It does not require Inkscape, Chrome, or any other external renderer.

## Build a single icon

```powershell
# 1. Render SVG → 7 PNG frames (16, 24, 32, 48, 64, 128, 256 px)
node render_svg.js clipboardpp.svg clipboardpp

# 2. Assemble PNGs → multi-resolution ICO
python make_ico.py clipboardpp

# Output: clipboardpp.ico
```

The first argument to `render_svg.js` is the SVG source file.  
The second argument is the filename prefix used for output PNGs.

The same prefix is passed to `make_ico.py`, which reads `<prefix>_16.png` through `<prefix>_256.png` and writes `<prefix>.ico`.

## Build all three icons

```powershell
cd icons
npm install   # first time only

foreach ($name in @("clipboardpp", "sqlite_editor", "json_viewer")) {
    node render_svg.js "$name.svg" $name
    python make_ico.py $name
}
```

Or individually:

```powershell
node render_svg.js clipboardpp.svg  clipboardpp  && python make_ico.py clipboardpp
node render_svg.js sqlite_editor.svg sqlite_editor && python make_ico.py sqlite_editor
node render_svg.js json_viewer.svg  json_viewer  && python make_ico.py json_viewer
```

## Output

The generated `.ico` files are placed directly in this folder:

```
icons/clipboardpp.ico
icons/sqlite_editor.ico
icons/json_viewer.ico
```

These are referenced by the Windows resource files:
- `resources/app.rc` → `clipboardpp.ico`
- `dbviewer/res/sqlite_editor.rc` → `sqlite_editor.ico`
- `jsonviewer/res/json_viewer.rc` → `json_viewer.ico`

After rebuilding an icon, rebuild the corresponding project to embed the new ICO.

## Customising an icon

Open the `.svg` in any text editor. The colors are defined in `<linearGradient>` and `<stop>` elements near the top. The Clipboard++ icon uses a blue board gradient, metallic clip, white paper, a red margin line, and four navy ruled lines.

The Clipboard++ in-app icon is also **theme-driven at runtime** — the `DrawClipboardIconAt()` function in `src/ui/MainWindow.cpp` redraws it procedurally using the active theme's `iconBoardTop`, `iconBoardBottom`, `iconPaper`, `iconMarginLine`, and `iconRuledLines` color fields. The compiled `.ico` is used only for the taskbar, Alt+Tab, and the system tray default state. The systray icon is regenerated via GDI in `TrayIcon::ApplyTheme()` whenever the theme changes.

## Sizes in the ICO

Each `.ico` contains all seven sizes so Windows picks the best resolution for each context:

| Size | Used for |
|---|---|
| 16×16 | System tray, Explorer small icons |
| 24×24 | Explorer list view |
| 32×32 | Explorer medium icons, taskbar (standard DPI) |
| 48×48 | Explorer large icons |
| 64×64 | About dialog, task manager |
| 128×128 | Explorer extra-large icons |
| 256×256 | High-DPI taskbar, Windows search results |

## Troubleshooting

**`Cannot find module '@resvg/resvg-js'`** — run `npm install` in the `icons/` folder.

**`python` not found** — on some systems Python 3 is invoked as `python3`. Edit the command or add a `python` alias.

**ICO looks blurry at small sizes** — the SVG uses fine detail that doesn't scale down well. Simplify the SVG at `16px` by reducing stroke widths or adding a size-specific simplified path.
