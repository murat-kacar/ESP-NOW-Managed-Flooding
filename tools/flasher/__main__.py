"""Entry point: python -m tools.flasher (from the repository root, in an ESP-IDF environment)."""

import sys
from tkinter import Tk, messagebox

from .config import idf_path
from .jobs import JobRunner
from .ui import App


def main() -> int:
    try:
        idf_path()
    except RuntimeError as e:
        root = Tk()
        root.withdraw()
        messagebox.showerror("ESP-IDF environment required", str(e))
        root.destroy()
        return 1
    App(JobRunner()).mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
