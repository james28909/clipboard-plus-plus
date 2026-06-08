import json
import sys
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk


APP_TITLE = "JSON Viewer"


class JsonViewer(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("1100x720")
        self.minsize(760, 480)

        self.current_path: Path | None = None
        self.data = None
        self.node_values: dict[str, object] = {}
        self.node_paths: dict[str, str] = {}
        self.search_results: list[str] = []
        self.search_index = -1

        self._configure_style()
        self._build_menu()
        self._build_layout()
        self._set_status("Open a .json file to begin.")

    def _configure_style(self) -> None:
        style = ttk.Style(self)
        if "vista" in style.theme_names():
            style.theme_use("vista")
        style.configure("Toolbar.TFrame", padding=(8, 6))
        style.configure("Status.TLabel", padding=(8, 4))

    def _build_menu(self) -> None:
        menu_bar = tk.Menu(self)

        file_menu = tk.Menu(menu_bar, tearoff=False)
        file_menu.add_command(label="Open...", accelerator="Ctrl+O", command=self.open_file)
        file_menu.add_command(label="Reload", accelerator="F5", command=self.reload_file)
        file_menu.add_separator()
        file_menu.add_command(label="Exit", command=self.destroy)
        menu_bar.add_cascade(label="File", menu=file_menu)

        view_menu = tk.Menu(menu_bar, tearoff=False)
        view_menu.add_command(label="Show Full Document", command=self.show_full_document)
        view_menu.add_command(label="Expand All", command=self.expand_all)
        view_menu.add_command(label="Collapse All", command=self.collapse_all)
        menu_bar.add_cascade(label="View", menu=view_menu)

        edit_menu = tk.Menu(menu_bar, tearoff=False)
        edit_menu.add_command(label="Copy JSON Path", command=self.copy_selected_path)
        edit_menu.add_command(label="Copy Selected Value", command=self.copy_selected_value)
        menu_bar.add_cascade(label="Edit", menu=edit_menu)

        self.config(menu=menu_bar)
        self.bind_all("<Control-o>", lambda _event: self.open_file())
        self.bind_all("<F5>", lambda _event: self.reload_file())
        self.bind_all("<Control-f>", lambda _event: self.search_entry.focus_set())
        self.bind_all("<Control-l>", lambda _event: self.show_full_document())
        self.bind_all("<Control-Shift-C>", lambda _event: self.copy_selected_path())
        self.bind_all("<Return>", lambda _event: self.find_next())

    def _build_layout(self) -> None:
        toolbar = ttk.Frame(self, style="Toolbar.TFrame")
        toolbar.pack(side=tk.TOP, fill=tk.X)

        ttk.Button(toolbar, text="Open JSON", command=self.open_file).pack(side=tk.LEFT)
        ttk.Button(toolbar, text="Reload", command=self.reload_file).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Separator(toolbar, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=10)

        ttk.Label(toolbar, text="Find").pack(side=tk.LEFT)
        self.search_var = tk.StringVar()
        self.search_var.trace_add("write", lambda *_args: self._refresh_search())
        self.search_entry = ttk.Entry(toolbar, textvariable=self.search_var, width=32)
        self.search_entry.pack(side=tk.LEFT, padx=(6, 4))
        ttk.Button(toolbar, text="Next", command=self.find_next).pack(side=tk.LEFT)

        self.file_label_var = tk.StringVar(value="No file loaded")
        ttk.Label(toolbar, textvariable=self.file_label_var).pack(side=tk.RIGHT)

        panes = ttk.PanedWindow(self, orient=tk.HORIZONTAL)
        panes.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        tree_frame = ttk.Frame(panes)
        tree_frame.rowconfigure(0, weight=1)
        tree_frame.columnconfigure(0, weight=1)

        self.tree = ttk.Treeview(tree_frame, columns=("type", "value"), show="tree headings")
        self.tree.heading("#0", text="Key / Index")
        self.tree.heading("type", text="Type")
        self.tree.heading("value", text="Value")
        self.tree.column("#0", width=260, minwidth=160)
        self.tree.column("type", width=100, minwidth=80, anchor=tk.CENTER)
        self.tree.column("value", width=380, minwidth=160)
        self.tree.grid(row=0, column=0, sticky="nsew")

        tree_scroll_y = ttk.Scrollbar(tree_frame, orient=tk.VERTICAL, command=self.tree.yview)
        tree_scroll_y.grid(row=0, column=1, sticky="ns")
        tree_scroll_x = ttk.Scrollbar(tree_frame, orient=tk.HORIZONTAL, command=self.tree.xview)
        tree_scroll_x.grid(row=1, column=0, sticky="ew")
        self.tree.configure(yscrollcommand=tree_scroll_y.set, xscrollcommand=tree_scroll_x.set)

        text_frame = ttk.Frame(panes)
        text_frame.rowconfigure(2, weight=1)
        text_frame.columnconfigure(0, weight=1)

        details_bar = ttk.Frame(text_frame, style="Toolbar.TFrame")
        details_bar.grid(row=0, column=0, columnspan=2, sticky="ew")

        self.selected_path_var = tk.StringVar(value="$")
        self.selected_type_var = tk.StringVar(value="No selection")
        ttk.Label(details_bar, textvariable=self.selected_path_var).pack(side=tk.LEFT)
        ttk.Label(details_bar, textvariable=self.selected_type_var).pack(side=tk.LEFT, padx=(12, 0))
        ttk.Button(details_bar, text="Copy Path", command=self.copy_selected_path).pack(side=tk.RIGHT)
        ttk.Button(details_bar, text="Copy Value", command=self.copy_selected_value).pack(
            side=tk.RIGHT, padx=(0, 6)
        )
        ttk.Button(details_bar, text="Full JSON", command=self.show_full_document).pack(
            side=tk.RIGHT, padx=(0, 6)
        )

        ttk.Separator(text_frame, orient=tk.HORIZONTAL).grid(row=1, column=0, columnspan=2, sticky="ew")

        self.text = tk.Text(
            text_frame,
            wrap=tk.NONE,
            undo=False,
            font=("Consolas", 10),
            padx=8,
            pady=8,
        )
        self.text.grid(row=2, column=0, sticky="nsew")
        self.text.configure(state=tk.DISABLED)

        text_scroll_y = ttk.Scrollbar(text_frame, orient=tk.VERTICAL, command=self.text.yview)
        text_scroll_y.grid(row=2, column=1, sticky="ns")
        text_scroll_x = ttk.Scrollbar(text_frame, orient=tk.HORIZONTAL, command=self.text.xview)
        text_scroll_x.grid(row=3, column=0, sticky="ew")
        self.text.configure(yscrollcommand=text_scroll_y.set, xscrollcommand=text_scroll_x.set)
        self.tree.bind("<<TreeviewSelect>>", self._on_tree_selection)

        panes.add(tree_frame, weight=3)
        panes.add(text_frame, weight=2)

        self.status_var = tk.StringVar()
        ttk.Label(self, textvariable=self.status_var, anchor=tk.W, style="Status.TLabel").pack(
            side=tk.BOTTOM, fill=tk.X
        )

    def open_file(self) -> None:
        file_name = filedialog.askopenfilename(
            title="Open JSON file",
            filetypes=(("JSON files", "*.json"), ("All files", "*.*")),
        )
        if file_name:
            self.load_path(Path(file_name))

    def reload_file(self) -> None:
        if not self.current_path:
            self.open_file()
            return
        self.load_path(self.current_path)

    def load_path(self, path: Path) -> None:
        try:
            raw_text = path.read_text(encoding="utf-8-sig")
            data = json.loads(raw_text)
        except UnicodeDecodeError as exc:
            self._show_error(f"Could not read file as UTF-8:\n{exc}")
            return
        except json.JSONDecodeError as exc:
            self.current_path = path
            self.file_label_var.set(str(path))
            self._clear_views()
            self._set_text(path.read_text(encoding="utf-8-sig", errors="replace"))
            self._show_error(
                f"Invalid JSON in {path.name}\n\nLine {exc.lineno}, column {exc.colno}:\n{exc.msg}"
            )
            self._set_status(f"Invalid JSON: line {exc.lineno}, column {exc.colno}.")
            return
        except OSError as exc:
            self._show_error(f"Could not open file:\n{exc}")
            return

        self.current_path = path
        self.data = data
        self.file_label_var.set(str(path))
        self.title(f"{APP_TITLE} - {path.name}")
        self._render_json(data)
        self._set_status(f"Loaded {path.name} ({self._describe_value(data)}).")

    def _render_json(self, data) -> None:
        self._clear_views()
        self.tree.insert("", tk.END, iid="root", text="$", values=(self._type_name(data), self._preview(data)))
        self.node_values["root"] = data
        self.node_paths["root"] = "$"
        self._insert_tree_items("root", data, "$")
        self.tree.item("root", open=True)
        self.tree.selection_set("root")
        self.tree.focus("root")
        self._show_node("root")
        self._refresh_search()

    def _insert_tree_items(self, parent: str, value, parent_path: str) -> None:
        if isinstance(value, dict):
            for key, child in value.items():
                node_id = self.tree.insert(
                    parent,
                    tk.END,
                    text=str(key),
                    values=(self._type_name(child), self._preview(child)),
                )
                self.node_values[node_id] = child
                self.node_paths[node_id] = self._child_path(parent_path, key)
                self._insert_tree_items(node_id, child, self.node_paths[node_id])
        elif isinstance(value, list):
            for index, child in enumerate(value):
                node_id = self.tree.insert(
                    parent,
                    tk.END,
                    text=f"[{index}]",
                    values=(self._type_name(child), self._preview(child)),
                )
                self.node_values[node_id] = child
                self.node_paths[node_id] = f"{parent_path}[{index}]"
                self._insert_tree_items(node_id, child, self.node_paths[node_id])

    def _clear_views(self) -> None:
        self.tree.delete(*self.tree.get_children(""))
        self.node_values.clear()
        self.node_paths.clear()
        self.selected_path_var.set("$")
        self.selected_type_var.set("No selection")
        self._set_text("")
        self.search_results.clear()
        self.search_index = -1

    def _set_text(self, text: str) -> None:
        self.text.configure(state=tk.NORMAL)
        self.text.delete("1.0", tk.END)
        self.text.insert("1.0", text)
        self.text.configure(state=tk.DISABLED)

    def _on_tree_selection(self, _event=None) -> None:
        selected = self._selected_node()
        if selected:
            self._show_node(selected)

    def _show_node(self, node: str) -> None:
        value = self.node_values.get(node)
        path = self.node_paths.get(node, "$")
        self.selected_path_var.set(path)
        self.selected_type_var.set(self._describe_value(value))
        self._set_text(self._format_value(value))
        self._set_status(f"Viewing {path} ({self._describe_value(value)}).")

    def show_full_document(self) -> None:
        if self.data is None:
            return
        self.selected_path_var.set("$")
        self.selected_type_var.set(self._describe_value(self.data))
        self._set_text(self._format_value(self.data))
        self._set_status("Viewing full JSON document.")

    def copy_selected_path(self) -> None:
        path = self.node_paths.get(self._selected_node(), self.selected_path_var.get())
        if not path:
            return
        self.clipboard_clear()
        self.clipboard_append(path)
        self._set_status(f"Copied path: {path}")

    def copy_selected_value(self) -> None:
        node = self._selected_node()
        value = self.node_values.get(node, self.data)
        if value is None and node not in self.node_values:
            return
        text = self._format_value(value)
        self.clipboard_clear()
        self.clipboard_append(text)
        self._set_status("Copied selected JSON value.")

    def _selected_node(self) -> str:
        selection = self.tree.selection()
        if selection:
            return selection[0]
        focused = self.tree.focus()
        return focused if focused else ""

    def _refresh_search(self) -> None:
        query = self.search_var.get().strip().lower()
        self.search_results = []
        self.search_index = -1

        if not query:
            return

        def visit(node: str) -> None:
            label = self.tree.item(node, "text")
            values = " ".join(str(value) for value in self.tree.item(node, "values"))
            if query in f"{label} {values}".lower():
                self.search_results.append(node)
            for child in self.tree.get_children(node):
                visit(child)

        for root in self.tree.get_children(""):
            visit(root)

        self._set_status(f"Found {len(self.search_results)} matching tree item(s).")

    def find_next(self) -> None:
        if not self.search_results:
            self._refresh_search()
        if not self.search_results:
            return

        self.search_index = (self.search_index + 1) % len(self.search_results)
        node = self.search_results[self.search_index]
        self._open_parents(node)
        self.tree.selection_set(node)
        self.tree.focus(node)
        self.tree.see(node)
        self._show_node(node)
        self._set_status(f"Match {self.search_index + 1} of {len(self.search_results)}.")

    def expand_all(self) -> None:
        self._set_open_state(True)

    def collapse_all(self) -> None:
        self._set_open_state(False)
        for root in self.tree.get_children(""):
            self.tree.item(root, open=True)

    def _set_open_state(self, open_state: bool) -> None:
        def visit(node: str) -> None:
            self.tree.item(node, open=open_state)
            for child in self.tree.get_children(node):
                visit(child)

        for root in self.tree.get_children(""):
            visit(root)

    def _open_parents(self, node: str) -> None:
        parent = self.tree.parent(node)
        while parent:
            self.tree.item(parent, open=True)
            parent = self.tree.parent(parent)

    def _describe_value(self, value) -> str:
        if isinstance(value, dict):
            return f"object with {len(value)} key(s)"
        if isinstance(value, list):
            return f"array with {len(value)} item(s)"
        return self._type_name(value)

    def _format_value(self, value) -> str:
        return json.dumps(value, indent=2, ensure_ascii=False)

    def _child_path(self, parent_path: str, key: object) -> str:
        key_text = str(key)
        if key_text.isidentifier():
            return f"{parent_path}.{key_text}"
        escaped = key_text.replace("\\", "\\\\").replace("'", "\\'")
        return f"{parent_path}['{escaped}']"

    def _type_name(self, value) -> str:
        if value is None:
            return "null"
        if isinstance(value, bool):
            return "boolean"
        if isinstance(value, dict):
            return "object"
        if isinstance(value, list):
            return "array"
        if isinstance(value, str):
            return "string"
        if isinstance(value, int):
            return "integer"
        if isinstance(value, float):
            return "number"
        return type(value).__name__

    def _preview(self, value) -> str:
        if isinstance(value, dict):
            return f"{len(value)} key(s)"
        if isinstance(value, list):
            return f"{len(value)} item(s)"
        if value is None:
            return "null"
        text = json.dumps(value, ensure_ascii=False)
        return text if len(text) <= 140 else f"{text[:137]}..."

    def _show_error(self, message: str) -> None:
        messagebox.showerror(APP_TITLE, message)

    def _set_status(self, message: str) -> None:
        self.status_var.set(message)


def main() -> int:
    app = JsonViewer()
    if len(sys.argv) > 1:
        app.after(100, lambda: app.load_path(Path(sys.argv[1])))
    app.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
