# Popup toolbar layout prototypes

The popup toolbar must stay useful at the supported 360 px minimum width while
remaining understandable when more built-in and custom filters are added.
Controls use normal ImGui buttons so keyboard navigation, focus indication,
hover state, and DPI scaling remain consistent.

## Prototype A - soft-wrapped boundary groups (selected)

```text
+- FILTERS  [All 24] [Text] [URL] [File] [Code] [Secret] --+
| [JSON] [Email] [Color] [CMD] [custom filters ...]         |
+-----------------------------------------------------------+
+- ACTIONS  [Newline] [Move: Keep] [Paste selected 3] ------+
| [Clear selection] [configured program] [Settings]         |
+-----------------------------------------------------------+
+- DESTINATIONS  [Image] [Android] [Slots] -----------------+
+-----------------------------------------------------------+
```

- Every group uses a one-pixel line with its own theme-derived or user-selected
  color, with the compact label embedded into the first row.
- Buttons retain their natural width and soft-wrap as a unit before clipping.
- All filters remain directly visible; contextual multi-selection actions only
  appear while a multi-selection exists.
- The visual language stays close to the original lightweight button strip.

## Prototype B - single segmented row

```text
[All 24] [Text] [URL] | [NL] [Keep] | [Android] [Slots] | [...]
```

This is shorter, but group labels disappear at narrow widths and custom filters
quickly force every control into overflow. It becomes difficult to distinguish
an active filter from an active paste mode.

## Prototype C - accordion groups

```text
[Filters v] [Actions >] [Destinations >] [...]
[All 24] [Text] [URL] [Image] ...
```

This scales well but makes frequent actions require extra clicks and causes the
popup height to jump whenever a different group opens.

## Layout acceptance checks

- At 360 logical pixels, complete buttons wrap before crossing the content edge.
- At 125%, 150%, and 200% DPI, spacing and button dimensions use standard frame
  metrics and the effective UI scale instead of fixed physical button widths.
- No horizontal toolbar scrollbar is introduced and buttons are never shrunk.
- The active filter has a persistent active color and result-count badge.
- Every compact label has a tooltip; all controls use the same frame padding
  and minimum hit height.
