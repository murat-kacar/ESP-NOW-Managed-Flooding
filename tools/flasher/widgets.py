"""Reusable Tk widgets."""

import tkinter as tk
from tkinter import ttk


class ScrollableFrame(ttk.Frame):
    """A scrollable container (canvas + inner frame).

    The inner frame fills the visible width and grows wider when its content needs it;
    the horizontal scrollbar then scrolls the overflow. Mouse wheel scrolls vertically,
    Shift + mouse wheel horizontally.
    """

    def __init__(self, master: tk.Misc, height: int | None = None) -> None:
        super().__init__(master)
        self._canvas = tk.Canvas(self, highlightthickness=0, borderwidth=0)
        if height is not None:
            self._canvas.configure(height=height)
        vertical = ttk.Scrollbar(self, orient="vertical", command=self._canvas.yview)
        horizontal = ttk.Scrollbar(self, orient="horizontal", command=self._canvas.xview)
        self._canvas.configure(yscrollcommand=vertical.set, xscrollcommand=horizontal.set)
        self.inner = ttk.Frame(self._canvas)
        self._window = self._canvas.create_window((0, 0), window=self.inner, anchor="nw")
        self.inner.bind("<Configure>", self._on_resize)
        self._canvas.bind("<Configure>", self._on_resize)

        self._canvas.grid(row=0, column=0, sticky="nsew")
        vertical.grid(row=0, column=1, sticky="ns")
        horizontal.grid(row=1, column=0, sticky="we")
        self.rowconfigure(0, weight=1)
        self.columnconfigure(0, weight=1)
        self.bind_all("<MouseWheel>", self._on_wheel, add="+")
        self.bind_all("<Shift-MouseWheel>", self._on_shift_wheel, add="+")

    def scroll_to_top(self) -> None:
        self._canvas.xview_moveto(0)
        self._canvas.yview_moveto(0)

    def _on_resize(self, _: tk.Event) -> None:
        width = max(self._canvas.winfo_width(), self.inner.winfo_reqwidth())
        self._canvas.itemconfigure(self._window, width=width)
        self._canvas.configure(scrollregion=(0, 0, width, self.inner.winfo_reqheight()))

    def _contains(self, event: tk.Event) -> bool:
        widget = self.winfo_containing(event.x_root, event.y_root)
        while widget is not None:
            if widget is self:
                return True
            widget = widget.master
        return False

    def _on_wheel(self, event: tk.Event) -> None:
        if self._contains(event):
            self._canvas.yview_scroll(int(-event.delta / 120), "units")

    def _on_shift_wheel(self, event: tk.Event) -> None:
        if self._contains(event):
            self._canvas.xview_scroll(int(-event.delta / 120), "units")


class Tooltip:
    """Show a text box while the pointer is over a widget."""

    def __init__(self, widget: tk.Widget, text: str) -> None:
        self._widget = widget
        self._text = text
        self._window: tk.Toplevel | None = None
        widget.bind("<Enter>", self._show, add="+")
        widget.bind("<Leave>", self._hide, add="+")

    def _show(self, _: tk.Event) -> None:
        if self._window is not None:
            return
        x = self._widget.winfo_rootx() + 16
        y = self._widget.winfo_rooty() + self._widget.winfo_height() + 4
        self._window = tk.Toplevel(self._widget)
        self._window.wm_overrideredirect(True)
        self._window.wm_geometry(f"+{x}+{y}")
        tk.Label(
            self._window, text=self._text, justify="left", wraplength=360,
            background="#ffffe0", relief="solid", borderwidth=1, padx=6, pady=4,
        ).pack()

    def _hide(self, _: tk.Event) -> None:
        if self._window is not None:
            self._window.destroy()
            self._window = None
