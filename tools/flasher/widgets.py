"""Reusable Tk widgets."""

import tkinter as tk
from tkinter import ttk


class ScrollableFrame(ttk.Frame):
    """A vertically scrollable container (canvas + inner frame)."""

    def __init__(self, master: tk.Misc, height: int | None = None) -> None:
        super().__init__(master)
        self._canvas = tk.Canvas(self, highlightthickness=0, borderwidth=0)
        if height is not None:
            self._canvas.configure(height=height)
        scrollbar = ttk.Scrollbar(self, orient="vertical", command=self._canvas.yview)
        self.inner = ttk.Frame(self._canvas)
        self.inner.bind("<Configure>", lambda _: self._canvas.configure(scrollregion=self._canvas.bbox("all")))
        window = self._canvas.create_window((0, 0), window=self.inner, anchor="nw")
        self._canvas.bind("<Configure>", lambda e: self._canvas.itemconfigure(window, width=e.width))
        self._canvas.configure(yscrollcommand=scrollbar.set)
        self._canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        self.bind_all("<MouseWheel>", self._on_wheel, add="+")

    def scroll_to_top(self) -> None:
        self._canvas.yview_moveto(0)

    def _on_wheel(self, event: tk.Event) -> None:
        widget = self.winfo_containing(event.x_root, event.y_root)
        while widget is not None:
            if widget is self:
                self._canvas.yview_scroll(int(-event.delta / 120), "units")
                return
            widget = widget.master


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
