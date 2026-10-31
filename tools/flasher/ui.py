"""Tk user interface: build wizard on the left, detected devices on the right.

The left role selects the configuration and build. Each device row has its own role and image,
so different devices can be flashed with different firmware at the same time.
"""

import queue
import subprocess
import sys
import tkinter as tk
from collections.abc import Callable, Coroutine
from pathlib import Path
from tkinter import filedialog, messagebox, ttk
from typing import Any

import serial.tools.list_ports

from . import operations
from .config import (
    ADMIN_CODE_LENGTH_FALLBACK, ADMIN_CODE_LENGTH_OPTION, BUILDS_DIR, LEFT_COLUMN_WIDTH,
    PROFILE_DEVELOPMENT, PROFILE_PRODUCTION, ROLES, USER_CODE_LENGTH_FALLBACK, USER_CODE_LENGTH_OPTION,
    WINDOW_HEIGHT, WINDOW_WIDTH, dev_code, firmware_elf, firmware_images, latest_firmware,
)
from .factory_nvs import CodeLengths, FactoryCodes, random_code
from .jobs import Event, JobRunner
from .kconfig_form import ConfigForm, config_env_is_stale
from .widgets import ScrollableFrame

EVENT_POLL_MS = 50
MONITOR_POLL_MS = 500
LOG_MAX_LINES = 3000
CONFIG_FORM_HEIGHT = 250
CODE_ENTRY_WIDTH = 12
FIRMWARE_BOX_WIDTH = 34
ROLES_WITH_FACTORY_CODES = ("panel",)


class DeviceRow(ttk.LabelFrame):
    """One detected serial device with its role, image, codes, actions, progress and status."""

    def __init__(self, master: tk.Misc, app: "App", port: str, description: str, role: str) -> None:
        title = description if port in description else f"{description} ({port})"
        super().__init__(master, text=title, padding=8)
        self.app = app
        self.port = port
        self.busy = False
        self.locked = False  # secure boot or flash encryption enabled: no serial writes
        self._images: dict[str, Path] = {}
        self._info: dict[str, Any] = {}  # latest known device facts; a probe does not erase what a scan found

        self.info_var = tk.StringVar(value="Not probed")
        ttk.Label(self, textvariable=self.info_var).grid(row=0, column=0, sticky="w")

        image_line = ttk.Frame(self)
        image_line.grid(row=1, column=0, sticky="w", pady=(6, 0))
        ttk.Label(image_line, text="Role").pack(side="left")
        self.role_var = tk.StringVar(value=role)
        role_box = ttk.Combobox(image_line, textvariable=self.role_var, values=ROLES, state="readonly", width=10)
        role_box.pack(side="left", padx=(4, 12))
        role_box.bind("<<ComboboxSelected>>", lambda _: self._on_role_selected())
        ttk.Label(image_line, text="Firmware").pack(side="left")
        self.image_var = tk.StringVar()
        self.image_box = ttk.Combobox(image_line, textvariable=self.image_var, state="readonly",
                                      width=FIRMWARE_BOX_WIDTH)
        self.image_box.pack(side="left", padx=(4, 4))
        ttk.Button(image_line, text="Browse...", command=self._browse).pack(side="left")

        lengths = app.code_lengths()
        self.codes_frame = ttk.Frame(self)
        self.codes_frame.grid(row=2, column=0, sticky="w", pady=(6, 0))
        self.admin_var = tk.StringVar(value=dev_code(lengths.admin))
        self.user_var = tk.StringVar(value=dev_code(lengths.user))
        self._code_field(0, "Admin code", self.admin_var, lambda: app.code_lengths().admin)
        self._code_field(3, "User code", self.user_var, lambda: app.code_lengths().user)

        actions = ttk.Frame(self)
        actions.grid(row=3, column=0, sticky="w", pady=(6, 0))
        self.buttons: dict[str, ttk.Button] = {}
        for column, (name, command) in enumerate((
            ("Flash", app.flash),
            ("Flash NVS", app.flash_factory_nvs),
            ("Monitor", app.open_monitor),
            ("Factory reset", app.factory_reset),
            ("Erase flash", app.erase_flash),
            ("Scan", app.scan),
        )):
            button = ttk.Button(actions, text=name, command=lambda c=command: c(self))
            button.grid(row=0, column=column, padx=(0, 4))
            self.buttons[name] = button

        progress_line = ttk.Frame(self)
        progress_line.grid(row=4, column=0, sticky="we", pady=(6, 0))
        self.progress = ttk.Progressbar(progress_line, maximum=100, length=260, mode="determinate")
        self.progress.pack(side="left")
        self.status_var = tk.StringVar(value="Idle")
        self.status_label = ttk.Label(progress_line, textvariable=self.status_var)
        self.status_label.pack(side="left", padx=(8, 0))

        self._on_role_selected()

    @property
    def role(self) -> str:
        return self.role_var.get()

    def _code_field(self, column: int, label: str, var: tk.StringVar, length: Callable[[], int]) -> None:
        ttk.Label(self.codes_frame, text=label).grid(row=0, column=column, padx=(0 if column == 0 else 12, 4))
        validate = (self.register(lambda text: len(text) <= length() and (text == "" or text.isdigit())), "%P")
        ttk.Entry(
            self.codes_frame, textvariable=var, width=CODE_ENTRY_WIDTH, validate="key", validatecommand=validate,
        ).grid(row=0, column=column + 1)
        ttk.Button(
            self.codes_frame, text="Random", width=8, command=lambda: var.set(random_code(length())),
        ).grid(row=0, column=column + 2, padx=(4, 0))

    def _on_role_selected(self) -> None:
        if self.role in ROLES_WITH_FACTORY_CODES:
            self.codes_frame.grid()
            self.buttons["Flash NVS"].grid()
        else:
            self.codes_frame.grid_remove()
            self.buttons["Flash NVS"].grid_remove()
        self.refresh_images()

    def refresh_images(self, select_newest: bool = False) -> None:
        """List the images of this row's role, newest first."""
        self._images = self.app.firmware_choices(self.role)
        names = list(self._images)
        self.image_box.configure(values=names)
        if select_newest or self.image_var.get() not in self._images:
            self.image_var.set(names[0] if names else "")

    def _browse(self) -> None:
        image = self.app.browse_firmware(self.role)
        if image is None:
            return
        self.refresh_images()
        for name, candidate in self._images.items():
            if candidate == image:
                self.image_var.set(name)

    def selected_image(self) -> Path | None:
        return self._images.get(self.image_var.get())

    def codes(self) -> FactoryCodes:
        return FactoryCodes(admin_code=self.admin_var.get(), user_code=self.user_var.get())

    def apply_lengths(self, lengths: CodeLengths) -> None:
        """Replace codes that no longer fit the firmware's code lengths with the development defaults."""
        for var, length in ((self.admin_var, lengths.admin), (self.user_var, lengths.user)):
            if len(var.get()) != length:
                var.set(dev_code(length))

    def set_busy(self, busy: bool) -> None:
        self.busy = busy
        writes = ("Flash", "Flash NVS", "Factory reset", "Erase flash")
        for name, button in self.buttons.items():
            enabled = not busy and not (self.locked and name in writes)
            button.state(["!disabled"] if enabled else ["disabled"])

    def set_info(self, info: dict[str, Any] | None) -> None:
        if not info:
            return
        if info.get("mac") != self._info.get("mac"):
            self._info = {}  # another device on this port
        self._info.update(info)
        info = self._info
        parts = [info.get("chip", "?"), f"MAC {info.get('mac', '?')}"]
        if "flash_size" in info:
            parts.append(f"flash {info['flash_size']}")
        if "secure_boot" in info:
            parts.append(f"secure boot {'on' if info['secure_boot'] else 'off'}")
            parts.append(f"flash encryption {'on' if info['flash_encryption'] else 'off'}")
            self.locked = bool(info["secure_boot"] or info["flash_encryption"])
            self.set_busy(self.busy)
        self.info_var.set(" | ".join(parts))

    def set_status(self, text: str, ok: bool | None = None) -> None:
        self.status_var.set(text)
        self.status_label.configure(foreground={True: "#1a7f37", False: "#cf222e"}.get(ok, ""))


class App(tk.Tk):
    def __init__(self, runner: JobRunner) -> None:
        super().__init__()
        self.runner = runner
        self.title("ESP-NOW Doorbell Flasher")
        self._place_window()

        self.profile = tk.StringVar(value=PROFILE_DEVELOPMENT)
        self.role = tk.StringVar(value=ROLES[0])  # configuration and build role
        self.firmware_var = tk.StringVar()
        self._browsed_images: dict[str, list[Path]] = {role: [] for role in ROLES}
        self.rows: dict[str, DeviceRow] = {}
        self.monitors: dict[str, subprocess.Popen] = {}
        self.building = False
        self.configuring = False

        self.columnconfigure(1, weight=1)
        self.rowconfigure(0, weight=1)
        left = ttk.Frame(self, padding=12, width=LEFT_COLUMN_WIDTH)
        left.grid(row=0, column=0, sticky="ns")
        left.grid_propagate(False)
        right = ttk.Frame(self, padding=12)
        right.grid(row=0, column=1, sticky="nsew")
        self._build_wizard(left)
        self._build_devices(right)

        self.role.trace_add("write", lambda *_: self._on_role_change())
        self._on_role_change()
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.after(EVENT_POLL_MS, self._drain_events)
        self.after(MONITOR_POLL_MS, self._poll_monitors)

    # ----- layout -----

    def _place_window(self) -> None:
        self.update_idletasks()
        x = (self.winfo_screenwidth() - WINDOW_WIDTH) // 2
        y = (self.winfo_screenheight() - WINDOW_HEIGHT) // 2
        self.geometry(f"{WINDOW_WIDTH}x{WINDOW_HEIGHT}+{max(x, 0)}+{max(y, 0)}")
        self.resizable(False, False)

    def _build_wizard(self, parent: ttk.Frame) -> None:
        parent.columnconfigure(0, weight=1)
        ttk.Label(parent, text="Customization and build", font=("TkDefaultFont", 12, "bold")).grid(
            row=0, column=0, sticky="w", pady=(0, 8))

        profile = ttk.LabelFrame(parent, text="1. Profile", padding=8)
        profile.grid(row=1, column=0, sticky="we")
        ttk.Radiobutton(profile, text="Development (reversible)", value=PROFILE_DEVELOPMENT,
                        variable=self.profile).grid(row=0, column=0, sticky="w")
        ttk.Radiobutton(profile, text="Production (irreversible, burns eFuses)", value=PROFILE_PRODUCTION,
                        variable=self.profile, state="disabled").grid(row=1, column=0, sticky="w")
        ttk.Label(profile, text="Production is not defined yet (ADR 0004).", foreground="gray").grid(
            row=2, column=0, sticky="w")

        role = ttk.LabelFrame(parent, text="2. Role to configure and build", padding=8)
        role.grid(row=2, column=0, sticky="we", pady=(8, 0))
        for column, name in enumerate(ROLES):
            ttk.Radiobutton(role, text=name.capitalize(), value=name, variable=self.role).grid(
                row=0, column=column, sticky="w", padx=(0, 16))

        configuration = ttk.LabelFrame(parent, text="3. Configuration (hover a name for help)", padding=4)
        configuration.grid(row=3, column=0, sticky="we", pady=(8, 0))
        self.config_form = ConfigForm(configuration, height=CONFIG_FORM_HEIGHT)
        self.config_form.pack(fill="both", expand=True)

        build = ttk.LabelFrame(parent, text="4. Build", padding=8)
        build.grid(row=4, column=0, sticky="we", pady=(8, 0))
        self.build_button = ttk.Button(build, text="Build firmware", command=self.build)
        self.build_button.grid(row=0, column=0, sticky="w")
        ttk.Label(build, textvariable=self.firmware_var, wraplength=LEFT_COLUMN_WIDTH - 160).grid(
            row=0, column=1, sticky="w", padx=(8, 0))

        log_frame = ttk.LabelFrame(parent, text="Log", padding=4)
        log_frame.grid(row=5, column=0, sticky="nsew", pady=(8, 0))
        parent.rowconfigure(5, weight=1)
        self.log_text = tk.Text(log_frame, height=4, wrap="none", state="disabled", font=("Consolas", 8))
        scrollbar = ttk.Scrollbar(log_frame, orient="vertical", command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=scrollbar.set)
        self.log_text.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

    def _build_devices(self, parent: ttk.Frame) -> None:
        header = ttk.Frame(parent)
        header.pack(fill="x", pady=(0, 8))
        ttk.Label(header, text="Detection and flashing", font=("TkDefaultFont", 12, "bold")).pack(side="left")
        ttk.Button(header, text="Detect", command=self.detect).pack(side="right")
        self.device_list = ScrollableFrame(parent)
        self.device_list.pack(fill="both", expand=True)
        self.empty_label = ttk.Label(self.device_list.inner, text="Press Detect to list connected devices.")
        self.empty_label.pack(pady=20)

    # ----- events from the job thread -----

    def _drain_events(self) -> None:
        try:
            while True:
                self._handle(self.runner.events.get_nowait())
        except queue.Empty:
            pass
        self.after(EVENT_POLL_MS, self._drain_events)

    def _handle(self, event: Event) -> None:
        if event.kind == "log":
            self.log(f"[{event.key}] {event.payload}")
            return
        if event.kind == "build_done":
            self.building = False
            self._refresh_firmware(built_role=event.payload)
            self._update_build_button()
            return
        if event.kind == "config_ready":
            self._config_ready(*event.payload)
            return
        row = self.rows.get(event.key)
        if row is None:
            return
        if event.kind == "progress":
            row.progress["value"] = event.payload
        elif event.kind == "info":
            row.set_info(event.payload)
        elif event.kind == "status":
            row.set_status(*event.payload)
        elif event.kind == "busy":
            row.set_busy(event.payload)

    def log(self, line: str) -> None:
        self.log_text.configure(state="normal")
        self.log_text.insert("end", line + "\n")
        excess = int(self.log_text.index("end-1c").split(".")[0]) - LOG_MAX_LINES
        if excess > 0:
            self.log_text.delete("1.0", f"{excess + 1}.0")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    # ----- configuration -----

    def _on_role_change(self) -> None:
        self._refresh_firmware()
        self._load_config(self.role.get())

    def _load_config(self, role: str, after_reconfigure: bool = False) -> None:
        if not after_reconfigure and config_env_is_stale(role):
            # build/config.env lists the Kconfig files; ESP-IDF regenerates it on reconfigure.
            self.configuring = True
            self._update_build_button()
            self.config_form.show_message(f"Loading the ESP-IDF configuration for {role} (idf.py reconfigure)...")
            self.runner.submit(self._run_reconfigure(role))
            return
        try:
            self.config_form.load(role)
        except Exception as e:
            self.config_form.show_message(f"Could not load the configuration: {e}")
            self.log(f"[config] Could not load Kconfig for {role}: {e}")
        self._update_build_button()
        self._apply_code_lengths()

    async def _run_reconfigure(self, role: str) -> None:
        try:
            await operations.reconfigure(self.runner, role)
            self.runner.post("config", "config_ready", (role, True))
        except Exception as e:
            self.runner.post("config", "log", f"reconfigure failed: {e}")
            self.runner.post("config", "config_ready", (role, False))

    def _config_ready(self, role: str, ok: bool) -> None:
        self.configuring = False
        if role != self.role.get():
            self._load_config(self.role.get())
            return
        if ok:
            self._load_config(role, after_reconfigure=True)  # load once; never reconfigure in a loop
        else:
            self.config_form.show_message("idf.py reconfigure failed; see the log.")
            self._update_build_button()

    def _update_build_button(self) -> None:
        ready = self.config_form.loaded and not self.building and not self.configuring
        self.build_button.state(["!disabled"] if ready else ["disabled"])

    def code_lengths(self) -> CodeLengths:
        """Panel code lengths from the configuration form when it shows the panel, else the Kconfig defaults.

        The image does not carry its configuration, so the form must match the panel firmware being flashed.
        """
        def length(option: str, fallback: int) -> int:
            value = self.config_form.current_value(option) if hasattr(self, "config_form") else None
            return int(value) if value and value.isdigit() else fallback

        return CodeLengths(
            admin=length(ADMIN_CODE_LENGTH_OPTION, ADMIN_CODE_LENGTH_FALLBACK),
            user=length(USER_CODE_LENGTH_OPTION, USER_CODE_LENGTH_FALLBACK),
        )

    def _apply_code_lengths(self) -> None:
        lengths = self.code_lengths()
        for row in self.rows.values():
            row.apply_lengths(lengths)

    # ----- firmware images -----

    def firmware_choices(self, role: str) -> dict[str, Path]:
        """Display name -> image for a role: builds/ newest first, then images chosen with Browse."""
        choices = {}
        for image in firmware_images(role) + [p for p in self._browsed_images[role] if p.is_file()]:
            name = image.name if image.parent == BUILDS_DIR else f"{image.name} ({image.parent})"
            choices[name] = image
        return choices

    def browse_firmware(self, role: str) -> Path | None:
        path = filedialog.askopenfilename(
            parent=self, title=f"Select a merged {role} image (flashed at 0x0)",
            initialdir=BUILDS_DIR if BUILDS_DIR.is_dir() else None,
            filetypes=[("Firmware image", "*.bin"), ("All files", "*.*")],
        )
        if not path:
            return None
        image = Path(path).resolve()
        if image.parent != BUILDS_DIR and image not in self._browsed_images[role]:
            self._browsed_images[role].append(image)
        return image

    def _refresh_firmware(self, built_role: str | None = None) -> None:
        """Refresh the last-build label and every row's image list; rows of a just-built role select the new image."""
        newest = latest_firmware(self.role.get())
        self.firmware_var.set(f"Last build: {newest.name}" if newest else "Last build: none")
        for row in self.rows.values():
            row.refresh_images(select_newest=row.role == built_role)

    # ----- jobs -----

    def _start(self, row: DeviceRow, label: str, job: Callable[[], Coroutine[Any, Any, Any]]) -> None:
        if row.busy:
            return
        row.set_busy(True)
        row.progress["value"] = 0
        row.set_status(f"{label}...")
        self.runner.submit(self._run(row.port, label, job()))

    async def _run(self, key: str, label: str, job: Coroutine[Any, Any, Any]) -> None:
        try:
            await job
            self.runner.post(key, "status", (f"{label} done", True))
        except Exception as e:
            self.runner.post(key, "status", (f"{label} failed", False))
            self.runner.post(key, "log", f"{label} failed: {e}")
        finally:
            self.runner.post(key, "busy", False)

    def build(self) -> None:
        if self.building or self.configuring:
            return
        try:
            fragment = self.config_form.fragment()
        except ValueError as e:
            messagebox.showerror("Invalid configuration", str(e), parent=self)
            return
        role = self.role.get()
        self._apply_code_lengths()  # device rows follow the code lengths being built
        self.log(f"[build] {role}: " + (f"{fragment.count(chr(10))} changed option(s)" if fragment else "defaults"))
        self.building = True
        self._update_build_button()
        self.runner.submit(self._run_build(role, fragment))

    async def _run_build(self, role: str, fragment: str) -> None:
        try:
            await operations.build(self.runner, role, fragment)
            self.runner.post("build", "log", f"Build of {role} finished")
        except Exception as e:
            self.runner.post("build", "log", f"Build of {role} failed: {e}")
        finally:
            self.runner.post("build", "build_done", role)

    def detect(self) -> None:
        ports = {p.device: p.description for p in serial.tools.list_ports.comports()}
        for port in [p for p, row in self.rows.items() if p not in ports and not row.busy]:
            self.rows.pop(port).destroy()
        for port in sorted(ports):
            if port not in self.rows:
                row = DeviceRow(self.device_list.inner, self, port, ports[port], self.role.get())
                row.pack(fill="x", pady=(0, 8))
                self.rows[port] = row
        if self.rows:
            self.empty_label.pack_forget()
        else:
            self.empty_label.pack(pady=20)
        for row in self.rows.values():
            self._start(row, "Detect", lambda r=row: operations.probe(self.runner, r.port))

    def _image_or_warn(self, row: DeviceRow) -> Path | None:
        image = row.selected_image()
        if image is None:
            messagebox.showerror("No firmware", f"Build a {row.role} firmware or select one with Browse.", parent=self)
            return None
        if not image.is_file():
            messagebox.showerror("No firmware", f"The selected image no longer exists:\n{image}", parent=self)
            row.refresh_images()
            return None
        return image

    def _codes_or_warn(self, row: DeviceRow) -> FactoryCodes | None:
        codes = row.codes()
        try:
            codes.validate(self.code_lengths())
        except ValueError as e:
            messagebox.showerror("Invalid code", str(e), parent=self)
            return None
        return codes

    def flash(self, row: DeviceRow) -> None:
        role = row.role
        image = self._image_or_warn(row)
        if image is None:
            return
        codes = None
        if role in ROLES_WITH_FACTORY_CODES:
            codes = self._codes_or_warn(row)
            if codes is None:
                return
        profile = self.profile.get()
        self._start(row, "Flash", lambda: operations.flash(self.runner, row.port, role, profile, image, codes))

    def flash_factory_nvs(self, row: DeviceRow) -> None:
        codes = self._codes_or_warn(row)
        if codes is None:
            return
        role, profile = row.role, self.profile.get()
        self._start(row, "Flash NVS", lambda: operations.flash_factory_nvs(self.runner, row.port, role, profile, codes))

    def factory_reset(self, row: DeviceRow) -> None:
        if messagebox.askyesno(
            "Factory reset",
            f"Erase the user NVS partition on {row.port}?\n\n"
            "Pairings and changed codes are lost. Factory codes are kept.",
            icon="warning", parent=self,
        ):
            self._start(row, "Factory reset", lambda: operations.factory_reset(self.runner, row.port))

    def erase_flash(self, row: DeviceRow) -> None:
        if messagebox.askyesno(
            "Erase flash",
            f"Erase the entire flash on {row.port}?\n\nThe device will have no firmware until it is flashed again.",
            icon="warning", parent=self,
        ):
            self._start(row, "Erase flash", lambda: operations.erase_flash(self.runner, row.port))

    def scan(self, row: DeviceRow) -> None:
        self._start(row, "Scan", lambda: operations.scan(self.runner, row.port))

    def open_monitor(self, row: DeviceRow) -> None:
        if row.busy:
            return
        if sys.platform != "win32":
            messagebox.showerror("Monitor", "Opening a monitor console is only implemented on Windows.", parent=self)
            return
        cmd = [sys.executable, "-m", "esp_idf_monitor", "--port", row.port]
        elf = firmware_elf(row.role)
        if elf.exists():
            cmd.append(str(elf))
        self.monitors[row.port] = subprocess.Popen(cmd, creationflags=subprocess.CREATE_NEW_CONSOLE)
        row.set_busy(True)
        row.set_status("Monitor open")

    def _poll_monitors(self) -> None:
        for port, proc in list(self.monitors.items()):
            if proc.poll() is not None:
                del self.monitors[port]
                row = self.rows.get(port)
                if row is not None:
                    row.set_busy(False)
                    row.set_status("Monitor closed")
        self.after(MONITOR_POLL_MS, self._poll_monitors)

    def _on_close(self) -> None:
        running = (
            self.building or self.configuring
            or any(row.busy and row.port not in self.monitors for row in self.rows.values())
        )
        if running and not messagebox.askyesno("Quit", "Jobs are still running. Quit anyway?", parent=self):
            return
        self.destroy()
