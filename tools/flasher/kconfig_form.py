"""Firmware configuration form generated from the full ESP-IDF Kconfig tree of a role.

Loading follows ESP-IDF's own kconfgen/menuconfig flow: the environment from
apps/<role>/build/config.env (written by `idf.py reconfigure` or a build) is applied,
<IDF_PATH>/Kconfig is parsed with esp_kconfiglib, then sdkconfig.defaults is loaded.
Options therefore have the same defaults and visibility as in `idf.py menuconfig`.

Shown: every "Doorbell ..." menu (project Kconfig files), the espressif/esp-now menu,
and a selected set of ESP-IDF options. The form produces an sdkconfig fragment with only
the values changed from their defaults.
"""

import json
import os
import tkinter as tk
from collections.abc import Iterator
from pathlib import Path
from tkinter import ttk

from esp_kconfiglib import core as kc

from .config import REPO_ROOT, idf_path
from .widgets import ScrollableFrame, Tooltip

LABEL_WRAP = 180  # px; long option names wrap instead of widening the form
CHOICE_WIDTH = 18  # characters; longer choices are still readable in the drop-down list

PROJECT_MENU_PREFIX = "Doorbell"
COMPONENT_MENUS = ("ESP-NOW Configuration", "IoT Button", "LED Indicator")
IDF_SELECTED = (
    "ESP_DEFAULT_CPU_FREQ_MHZ",
    "ESP_BROWNOUT_DET",
    "ESP_BROWNOUT_DET_LVL_SEL",
    "ESP_TASK_WDT_EN",
    "ESP_TASK_WDT_PANIC",
    "ESP_TASK_WDT_TIMEOUT_S",
    "ESP_INT_WDT",
    "ESP_INT_WDT_TIMEOUT_MS",
    "ESP_CONSOLE_UART",
    "ESP_CONSOLE_UART_BAUDRATE",
    "ESPTOOLPY_FLASHMODE",
    "ESPTOOLPY_FLASHFREQ",
    "ESPTOOLPY_FLASHSIZE",
    "LOG_DEFAULT_LEVEL",
)


def config_env_file(role: str) -> Path:
    return REPO_ROOT / "apps" / role / "build" / "config.env"


def config_env_is_stale(role: str) -> bool:
    """True if config.env does not list exactly the Kconfig files of the role's build components.

    Only the file list matters: the form parses the Kconfig files themselves, so content changes need no
    reconfigure. File times cannot be used, because CMake's configure_file() keeps an unchanged config.env as is.
    The build components come from ESP-IDF's project_description.json (written with config.env).
    """
    build_dir = REPO_ROOT / "apps" / role / "build"
    env_file = build_dir / "config.env"
    description_file = build_dir / "project_description.json"
    if not env_file.is_file() or not description_file.is_file():
        return True
    env = json.loads(env_file.read_text(encoding="utf-8"))
    listed = {
        Path(path).resolve()
        for key in ("COMPONENT_KCONFIGS", "COMPONENT_KCONFIGS_PROJBUILD")
        for path in env.get(key, "").split(";") if path
    }
    if any(not path.is_file() for path in listed):
        return True
    description = json.loads(description_file.read_text(encoding="utf-8"))
    expected = {
        (Path(info["dir"]) / name).resolve()
        for info in description.get("build_component_info", {}).values() if info.get("dir")
        for name in ("Kconfig", "Kconfig.projbuild")
        if (Path(info["dir"]) / name).is_file()
    }
    return not expected <= listed


def load_kconfig(role: str) -> kc.Kconfig:
    env = json.loads(config_env_file(role).read_text(encoding="utf-8"))
    env["IDF_MINIMAL_BUILD"] = "y"  # projects set MINIMAL_BUILD ON (tools/cmake/kconfig.cmake passes this)
    # esp_kconfiglib expands $VARS from os.environ while parsing, so apply the build environment temporarily.
    saved = os.environ.copy()
    try:
        os.environ.update(env)
        kconfig = kc.Kconfig(str(idf_path() / "Kconfig"), warn=False, info=False)
    finally:
        os.environ.clear()
        os.environ.update(saved)
    app = REPO_ROOT / "apps" / role
    for defaults in (app / "sdkconfig.defaults", app / "sdkconfig.defaults.esp32"):
        if defaults.is_file():
            kconfig.load_config(str(defaults), replace=False)
    return kconfig


def _active_range(sym: kc.Symbol) -> tuple[int, int] | None:
    base = 16 if sym.type == kc.HEX else 10
    for low, high, cond, *_ in sym.ranges:
        if kc.expr_value(cond):
            return int(low.str_value, base), int(high.str_value, base)
    return None


def _walk(node) -> Iterator:
    """All nodes in a sibling list and their children."""
    while node is not None:
        yield node
        if node.list is not None:
            yield from _walk(node.list)
        node = node.next


def _is_option(node) -> bool:
    item = node.item
    if not node.prompt:
        return False
    return isinstance(item, kc.Choice) or (isinstance(item, kc.Symbol) and item.choice is None)


class ConfigForm(ttk.Frame):
    """Renders the selected Kconfig sections as Tk widgets and collects changed values."""

    def __init__(self, master: tk.Misc, height: int) -> None:
        super().__init__(master)
        self._scroll = ScrollableFrame(self, height=height)
        self._scroll.pack(fill="both", expand=True)
        self._kconfig: kc.Kconfig | None = None
        self._role = ""
        self._sections: list[tuple[str, list]] = []  # (title, top nodes)
        self._tracked: list = []  # option nodes included in the fragment
        self._defaults: dict[str, str] = {}
        self._entries: dict[str, tk.StringVar] = {}  # int, hex and string symbols, applied on collect

    # ----- public -----

    @property
    def loaded(self) -> bool:
        return self._kconfig is not None

    def show_message(self, text: str) -> None:
        self._kconfig = None
        self._clear()
        ttk.Label(self._scroll.inner, text=text, wraplength=330, foreground="gray").grid(row=0, column=0, pady=12)

    def load(self, role: str) -> None:
        self._role = role
        self._kconfig = load_kconfig(role)
        self._sections = self._find_sections()
        self._tracked = [n for _, tops in self._sections for top in tops for n in self._option_nodes(top)]
        # Defaults of options that are visible now. A hidden option (its parent is off) has no value yet: kconfiglib
        # reports "" for it, so its default is taken when it first becomes visible (see _render_option).
        self._defaults = {node.item.name: self._value(node.item) for node in self._tracked if node.item.visibility}
        self._render()
        self._scroll.scroll_to_top()

    def reset(self) -> None:
        self.load(self._role)

    def default_value(self, name: str) -> str | None:
        return self._defaults.get(name)

    def current_value(self, name: str) -> str | None:
        """Current value of a symbol, including values typed but not yet collected."""
        if self._kconfig is None or name not in self._kconfig.syms:
            return None
        self._apply_entries_quietly()
        return self._kconfig.syms[name].str_value

    def fragment(self) -> str:
        """Return sdkconfig lines for values changed from their defaults. Raises ValueError if a value is invalid."""
        if self._kconfig is None:
            raise ValueError("The configuration is not loaded yet.")
        self._apply_entries()
        lines = []
        for name, value in self._snapshot().items():
            if name not in self._defaults or value == self._defaults[name]:
                continue  # never shown, so never changed; or unchanged
            sym = self._kconfig.syms[value] if name in self._kconfig.named_choices else self._kconfig.syms[name]
            lines.append(sym.config_string)
        return "".join(lines)

    # ----- model -----

    def _find_sections(self) -> list[tuple[str, list]]:
        project, component = [], []
        for node in self._kconfig.node_iter():
            if node.item != kc.MENU or not node.prompt:
                continue
            title = node.prompt[0]
            if title.startswith(PROJECT_MENU_PREFIX):
                project.append(node)
            elif title in COMPONENT_MENUS:
                component.append(node)
        idf = []
        for name in IDF_SELECTED:
            item = self._kconfig.named_choices.get(name) or self._kconfig.syms.get(name)
            if item is not None and item.nodes:
                idf.append(item.nodes[0])
        return [("", project), ("", component), ("ESP-IDF (selected options)", idf)]

    @staticmethod
    def _option_nodes(top) -> list:
        if top.item == kc.MENU:
            return [n for n in _walk(top.list) if _is_option(n)]
        return [top] if _is_option(top) else []

    @staticmethod
    def _value(item) -> str:
        if isinstance(item, kc.Choice):
            return item.selection.name if item.selection else ""
        return item.str_value

    def _snapshot(self) -> dict[str, str]:
        return {node.item.name: self._value(node.item) for node in self._tracked}

    def _apply_entries(self) -> None:
        for name, var in self._entries.items():
            sym = self._kconfig.syms[name]
            if not sym.visibility:
                continue
            text = var.get().strip()
            label = sym.nodes[0].prompt[0]
            if sym.type in (kc.INT, kc.HEX):
                base = 16 if sym.type == kc.HEX else 10
                try:
                    number = int(text, base)
                except ValueError:
                    raise ValueError(f"{label}: '{text}' is not a valid number.") from None
                limits = _active_range(sym)
                if limits and not limits[0] <= number <= limits[1]:
                    raise ValueError(f"{label}: must be between {limits[0]} and {limits[1]}.")
                text = hex(number) if sym.type == kc.HEX else str(number)
            sym.set_value(text)

    def _apply_entries_quietly(self) -> None:
        """Keep typed values across a re-render; invalid ones are reported on collect."""
        try:
            self._apply_entries()
        except ValueError:
            pass

    # ----- view -----

    def _clear(self) -> None:
        for child in self._scroll.inner.winfo_children():
            child.destroy()
        self._entries = {}
        self._row = 0

    def _render(self) -> None:
        self._clear()
        for title, tops in self._sections:
            if title and tops:
                self._header(title, 0)
            for top in tops:
                if top.item == kc.MENU:
                    self._header(top.prompt[0], 0)
                    self._render_nodes(top.list, depth=1)
                else:
                    self._render_option(top, depth=1)
        ttk.Button(self._scroll.inner, text="Reset to defaults", command=self.reset).grid(
            row=self._row, column=0, columnspan=2, sticky="w", pady=(8, 0))

    def _render_nodes(self, node, depth: int) -> None:
        while node is not None:
            item = node.item
            if item == kc.MENU and kc.expr_value(node.dep):
                self._header(node.prompt[0], depth)
                self._render_nodes(node.list, depth + 1)
            elif item == kc.COMMENT and kc.expr_value(node.dep):
                self._comment(node.prompt[0], depth)
            elif _is_option(node):
                self._render_option(node, depth)
                if isinstance(item, kc.Symbol) and node.list is not None:
                    self._render_nodes(node.list, depth + 1)
            node = node.next

    def _render_option(self, node, depth: int) -> None:
        item = node.item
        if not item.visibility:
            return
        # First time visible: the user could not edit it while hidden, so its value now is its default under the
        # current parent settings.
        self._defaults.setdefault(item.name, self._value(item))
        if isinstance(item, kc.Choice):
            self._choice_field(node, item, depth)
        else:
            self._symbol_field(node, item, depth)

    def _place(self, label: tk.Widget, field: tk.Widget, depth: int, help_text: str) -> None:
        label.grid(row=self._row, column=0, sticky="w", padx=(depth * 12, 6), pady=1)
        field.grid(row=self._row, column=1, sticky="w", pady=1)
        if help_text:
            Tooltip(label, help_text)
        self._row += 1

    def _header(self, text: str, depth: int) -> None:
        label = ttk.Label(self._scroll.inner, text=text, font=("TkDefaultFont", 9, "bold"))
        label.grid(row=self._row, column=0, columnspan=2, sticky="w", padx=(depth * 12, 0), pady=(6, 1))
        self._row += 1

    def _comment(self, text: str, depth: int) -> None:
        label = ttk.Label(self._scroll.inner, text=text, foreground="gray", wraplength=330)
        label.grid(row=self._row, column=0, columnspan=2, sticky="w", padx=(depth * 12, 0))
        self._row += 1

    def _choice_field(self, node, choice: kc.Choice, depth: int) -> None:
        options = {sym.nodes[0].prompt[0]: sym for sym in choice.syms if sym.visibility}
        var = tk.StringVar(value=choice.selection.nodes[0].prompt[0] if choice.selection else "")
        box = ttk.Combobox(
            self._scroll.inner, textvariable=var, values=list(options), state="readonly", width=CHOICE_WIDTH,
        )

        def on_select(_: tk.Event) -> None:
            options[var.get()].set_value(2)
            self._apply_entries_quietly()
            self._render()

        box.bind("<<ComboboxSelected>>", on_select)
        default = self._defaults.get(choice.name)
        default_prompt = self._kconfig.syms[default].nodes[0].prompt[0] if default else ""
        help_text = f"Default: {default_prompt}.\n{node.help or ''}" if default_prompt else (node.help or "")
        self._place(ttk.Label(self._scroll.inner, text=node.prompt[0], wraplength=LABEL_WRAP), box, depth, help_text.strip())

    def _symbol_field(self, node, sym: kc.Symbol, depth: int) -> None:
        label = ttk.Label(self._scroll.inner, text=node.prompt[0], wraplength=LABEL_WRAP)
        default = self._defaults[sym.name]
        if sym.type == kc.BOOL:
            var = tk.BooleanVar(value=sym.str_value == "y")

            def on_toggle() -> None:
                sym.set_value(2 if var.get() else 0)
                self._apply_entries_quietly()
                self._render()

            field = ttk.Checkbutton(self._scroll.inner, variable=var, command=on_toggle)
            prefix = f"Default: {'on' if default == 'y' else 'off'}."
        else:
            var = tk.StringVar(value=sym.str_value)
            self._entries[sym.name] = var
            width = 16 if sym.type == kc.STRING else 10
            field = ttk.Entry(self._scroll.inner, textvariable=var, width=width)
            limits = _active_range(sym) if sym.type in (kc.INT, kc.HEX) else None
            prefix = f"Range: {limits[0]} to {limits[1]}. " if limits else ""
            prefix += f"Default: {default!r}." if sym.type == kc.STRING else f"Default: {default}."
        self._place(label, field, depth, f"{prefix}\n{node.help or ''}".strip())
