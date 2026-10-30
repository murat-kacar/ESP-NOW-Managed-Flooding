"""Asyncio job runner on a background thread.

Tkinter is not thread-safe, so jobs never touch widgets: they post Events to a
queue that the Tk main loop drains with after().
"""

import asyncio
import json
import queue
import subprocess
import sys
import threading
from concurrent.futures import Future
from pathlib import Path
from typing import Any, Coroutine, NamedTuple

WORKER = Path(__file__).with_name("esptool_worker.py")
_NO_WINDOW = {"creationflags": subprocess.CREATE_NO_WINDOW} if sys.platform == "win32" else {}


class JobError(Exception):
    pass


class Event(NamedTuple):
    key: str  # serial port, or "build"
    kind: str  # log | progress | info | status | busy | build_done
    payload: Any = None


class JobRunner:
    def __init__(self) -> None:
        self.events: queue.Queue[Event] = queue.Queue()
        self._loop = asyncio.new_event_loop()
        threading.Thread(target=self._loop.run_forever, name="jobs", daemon=True).start()

    def post(self, key: str, kind: str, payload: Any = None) -> None:
        self.events.put(Event(key, kind, payload))

    def submit(self, coro: Coroutine[Any, Any, Any]) -> Future:
        return asyncio.run_coroutine_threadsafe(coro, self._loop)

    async def worker(self, key: str, *args: str) -> dict[str, Any] | None:
        """Run esptool_worker.py, forward its log and progress events, return its info data."""
        proc = await asyncio.create_subprocess_exec(
            sys.executable, str(WORKER), *args,
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT, **_NO_WINDOW,
        )
        info: dict[str, Any] | None = None
        ok = False
        error: str | None = None
        async for raw in proc.stdout:
            line = raw.decode(errors="replace").strip()
            if not line:
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                self.post(key, "log", line)
                continue
            kind = event.get("type")
            if kind == "log":
                self.post(key, "log", event["text"])
            elif kind == "progress":
                self.post(key, "progress", event["percent"])
            elif kind == "info":
                info = event["data"]
            elif kind == "result":
                ok = bool(event["ok"])
                error = event.get("error")
        await proc.wait()
        if not ok or proc.returncode != 0:
            raise JobError(error or f"worker exited with code {proc.returncode}")
        return info

    async def command(self, key: str, *cmd: str) -> None:
        """Run a plain command and forward its output lines as log events."""
        proc = await asyncio.create_subprocess_exec(
            *cmd, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT, **_NO_WINDOW,
        )
        async for raw in proc.stdout:
            line = raw.decode(errors="replace").rstrip()
            if line:
                self.post(key, "log", line)
        await proc.wait()
        if proc.returncode != 0:
            raise JobError(f"command exited with code {proc.returncode}")
