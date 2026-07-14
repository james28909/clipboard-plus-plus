# Settings UI architecture

Clipboard++ Settings uses feature ownership instead of a catch-all Global page. A setting appears in one primary location; nearby pages link to that owner when the relationship is useful.

## Navigation and ownership

| Top-level page | Subpage | Owns |
|---|---|---|
| General | - | Windows startup and interface-help preferences |
| Clipboard | History | Ordering, capacity, deduplication, encrypted retention, overflow vault, and live-history preview |
| Clipboard | Profiles | Profile selection, creation, rename/delete, process bindings, and automatic switching |
| Clipboard | Capture Rules | Saved filters, matching conditions, and profile destinations |
| Clipboard | Paste Tools | Regex transforms, paste templates, and structured formatting actions |
| Clipboard | Images | Image capture, retention, database statistics, and destructive cleanup |
| Popup | - | Outside-click dismissal and post-paste behavior |
| Hotkeys | - | Application shortcuts, history/profile slot banks, named-slot shortcuts, route priority, and pass-through keys |
| Appearance | Themes / Popup / Window & Layout / Font & Icon / Colors | Visual appearance only |
| Integrations | Editor | External editor behavior and shortcut link |
| Integrations | Android | Receiver, endpoint, companion setup, and shortcut link |
| Privacy | - | Incognito capture state, planned privacy controls, and encryption status |
| Developer (Debug) | General / Diagnostics / Inspectors | Debug-only experiments, diagnostics, and clipboard inspection |
| Support & diagnostics | - | Privacy-reviewed support ZIPs, issue Markdown, and safe GitHub issue launching |
| About | - | Product identity, component versions, and license |

Hotkeys remains standalone because bindings affect several features. Popup, Profiles, Named slots, Editor, and Android either contain their shortcut controls or link directly to Hotkeys.

The sidebar is intentionally flat. It has no category headings such as Core, Tools, or System; the feature names provide the navigation structure without another labeling layer.

## Compact layout rules

- Content padding is 18 px horizontally and 16 px vertically; cards use 12 x 9 px padding and 7 x 6 px item spacing.
- A page begins with one title and short description. Nested tabs do not repeat the page heading.
- Related controls live in bordered cards. Explanations use short muted status text; longer details belong in tooltips or documentation.
- Tables use uniform 7 x 4 px cell padding, borders, alternating rows, aligned headers, and stable action columns.
- Tables combine stretch columns with stable control/action widths. They never create a full-height nested scrolling region; the page remains the sole owner of vertical scrolling.
- Saved-entry tools are list-first. New/Edit reveals the form; Save and Cancel complete or leave editing; Delete remains a destructive action beside the saved entry.
- Empty, inactive, warning, success, and error states use the shared Settings status palette. Controls that are not implemented are visibly disabled and explicitly described as inactive.
- Controls wrap or scroll instead of overlapping at the minimum window width. Custom fonts, UI scale, and Windows DPI scale must not change ownership or alignment.

## Manual visual verification

Use the Debug build and inspect every top-level page and subpage at 100%, 125%, and 150% Windows scaling, plus the minimum supported window size. At each size verify:

- no clipped labels, overlapping columns, inaccessible buttons, or off-screen editors;
- the page scrolls only while the pointer is over its background, and nested lists/tables own their relevant scrolling;
- New/Edit forms stay hidden until requested and Save/Cancel/Delete placement remains predictable;
- empty, disabled, warning, error, and loading text is readable with the current theme;
- long profile/filter/slot/transform/template names and a large custom font do not jumble table headers or actions;
- cross-links open the expected top-level page and nested tab.
