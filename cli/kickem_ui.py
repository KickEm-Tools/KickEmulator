# SPDX-License-Identifier: AGPL-3.0-or-later
"""kickem_ui: what `kickem` prints while it works. Python 3.8+ standard library only.

Two ways of printing, chosen per run:

* **Compact** (stderr is a terminal): one line per step. The running step is redrawn in place
  with a spinner, its progress (git's, CMake's `[ 45%]`, Ninja's `[412/980]`, Docker's steps,
  bytes downloaded) and the time so far; a finished step stays as `✔ <step>  <result>  <time>`
  (or `✘` and the end of its log). A dim footer names the keys: Ctrl-O (or `v`) switches to the
  details view, which streams what runs underneath (git, cmake, the compiler, docker, QEMU, the
  app's and the panel's logs) as it is written, and back. `--verbose` starts in the details
  view. Colour unless NO_COLOR is set or --no-color is given. While it runs, the process's own
  stdout and stderr go to a log (`<cache>/logs/kickem-<command>.log`), so nothing a child prints
  breaks the redrawn lines; stdout lines meant for scripts still reach the real stdout.
* **Plain** (stderr is not a terminal: a pipe, a file, CI, the checks): stable lines, one per
  event, as `kickem` always printed them (`kickem: <message>`), plus a line when a step starts
  and ends:

      kickem: [....] Build Kick
      kickem: [ ok ] Build Kick  141.2 s
      kickem: [FAIL] Build Kick  3.1 s

  and a progress line at most every 30 s while a long step runs. --verbose streams the steps'
  logs too.

The terminal is put in non-canonical, no-echo mode (ISIG kept, so Ctrl-C still stops; IEXTEN
cleared, so the terminal's own Ctrl-O, VDISCARD, does not swallow output) only while a compact
view is shown, and restored on exit, on Ctrl-C, on errors and on SIGTERM.
"""

import atexit
import os
import re
import select
import signal
import sys
import threading
import time

try:
    import termios
except ImportError:  # not a POSIX terminal: no keys, the rest works
    termios = None

SPINNER = "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏"
OK, FAIL, WARN, DOT = "✔", "✘", "!", "·"
ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
STYLES = {"green": "32", "red": "31", "yellow": "33", "dim": "2", "bold": "1", "cyan": "36", "boldred": "1;31"}
KEY_DETAILS = (b"\x0f", b"v", b"V")   # Ctrl-O, v
TAIL_LINES = 40


def fmt_time(s):
    if s < 60:
        return "%.1f s" % s
    m, s = divmod(int(s + 0.5), 60)
    if m < 60:
        return "%dm%02ds" % (m, s)
    h, m = divmod(m, 60)
    return "%dh%02dm" % (h, m)


def fmt_bytes(n):
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1000 or unit == "GB":
            return ("%.0f %s" if unit in ("B", "KB") or n >= 100 else "%.1f %s") % (n, unit)
        n /= 1000.0
    return "%.1f GB" % n


def tail_text(path, offset=0, lines=TAIL_LINES):
    """The last LINES lines of PATH after byte OFFSET ('' if unreadable)."""
    try:
        with open(path, "rb") as f:
            f.seek(0, os.SEEK_END)
            size = f.tell()
            start = max(offset, size - 256 * 1024)
            f.seek(start)
            data = f.read().decode("utf-8", "replace")
    except OSError:
        return ""
    data = data.replace("\r\n", "\n")
    out = []
    for line in data.split("\n"):
        out.append(line.rsplit("\r", 1)[-1] if "\r" in line.rstrip("\r") else line.rstrip("\r"))
    out = [l for l in out if l.strip()]
    return "\n".join(out[-lines:])


# --- Progress parsers: a log line in, a short progress text out (or None) --------------------
GIT_RX = re.compile(r"^(Receiving objects|Resolving deltas|Counting objects|Compressing objects|Updating files):\s+(\d+)%(?:\s+\([^)]*\))?(?:,\s*([\d.]+ \w?iB))?")
MAKE_RX = re.compile(r"^\[\s*(\d+)%\]")
NINJA_RX = re.compile(r"^\[(\d+)/(\d+)\]")
DOCKER_RX = re.compile(r"^#\d+ \[(?:[\w-]+ )?(\d+)/(\d+)\] (.*)")


def parse_git(line):
    m = GIT_RX.match(line)
    if not m:
        return None
    what = {"Receiving objects": "receiving", "Resolving deltas": "resolving deltas", "Counting objects": "counting",
            "Compressing objects": "compressing", "Updating files": "checking out"}[m.group(1)]
    return "%s %s%%%s" % (what, m.group(2), ("  " + m.group(3)) if m.group(3) else "")


def parse_build(line):
    """CMake's Makefile `[ 45%]`, Ninja's `[412/980]`, Docker BuildKit's `#7 [3/6] RUN ...`."""
    m = NINJA_RX.match(line)
    if m:
        return "[%s/%s]" % (m.group(1), m.group(2))
    m = MAKE_RX.match(line)
    if m:
        return "[%3s%%]" % m.group(1)
    m = DOCKER_RX.match(line)
    if m:
        return "[%s/%s] %s" % (m.group(1), m.group(2), m.group(3)[:60])
    return None


class Failed(Exception):
    """A failure with a message for the user; `reported` once a step has printed it."""

    def __init__(self, msg, code=1, log=None):
        Exception.__init__(self, msg)
        self.msg, self.code, self.log, self.reported = msg, code, log, False


class Step:
    def __init__(self, ui, label, log=None, parse=None, poll=None, poll_every=1.0, detail_log=True):
        self.ui, self.label, self.log, self.parse, self.poll = ui, label, log, parse, poll
        self.poll_every, self.next_poll = poll_every, 0.0
        self.t0 = time.monotonic()
        self.progress, self.result, self.status = "", "", None
        self.log_off = _size(log) if log else 0
        self.plain_last = (self.t0, "")
        if log and detail_log:
            ui.follow(log)

    # Called by the code doing the work.
    def set(self, progress):
        self.progress = progress or ""

    def done(self, result=""):
        self.result = result

    def next(self, label, result=""):
        """Finishes this step's current part as done and goes on under a new label."""
        self.ui._finish(self, "ok", result)
        self.label, self.t0, self.progress, self.result = label, time.monotonic(), "", ""
        self.ui._start(self)

    def elapsed(self):
        return time.monotonic() - self.t0

    def __enter__(self):
        self.ui._start(self)
        return self

    def __exit__(self, et, ev, tb):
        if et is None:
            self.ui._finish(self, "ok", self.result)
            return False
        if et is KeyboardInterrupt:
            self.ui._finish(self, "fail", "interrupted")
            return False
        if et is SystemExit and not (ev.code or 0):
            self.ui._finish(self, "ok", self.result)
            return False
        msg, log = None, self.log
        if isinstance(ev, Failed):
            msg, log = ev.msg, ev.log or self.log
            ev.reported = True
        elif et is not SystemExit:
            msg = "%s: %s" % (et.__name__, ev)
        self.ui._finish(self, "fail", "", msg=msg, log=log, log_off=self.log_off if log == self.log else 0)
        return False


def _size(path):
    try:
        return os.path.getsize(path)
    except OSError:
        return 0


class UI:
    def __init__(self, prefix="kickem: "):
        self.prefix = prefix
        self.command = None
        self.fancy = False       # the compact, redrawn view (stderr is a terminal)
        self.color = False
        self.verbose = False
        self.details = False     # the details view is on
        self.lock = threading.RLock()
        self.steps = []          # active steps, innermost last
        self.followed = {}       # path -> [offset, label, partial]
        self.live = 0            # lines of the live region on screen
        self.term = sys.stderr   # where the compact view is drawn
        self.term_out = sys.stdout
        self.stdout_tty = False
        self.log_path = None
        self.keys = False
        self.footer = True
        self._tio = None
        self._stop = threading.Event()
        self._threads = []
        self._saved_fds = None
        self._frame = 0
        self._started = False

    # --- Setup ---------------------------------------------------------------------------------
    def configure(self, no_color=False, verbose=False, interactive=True):
        tty = _isatty(sys.stderr) and os.environ.get("TERM", "") != "dumb"
        self.fancy = tty and interactive
        self.color = tty and not no_color and not os.environ.get("NO_COLOR")
        self.verbose = verbose
        self.details = verbose
        self.stdout_tty = _isatty(sys.stdout)

    def start(self, log_path=None):
        """Starts the compact view (fancy) or, in plain mode with --verbose, the log streamer."""
        if self._started:
            return
        self._started = True
        if self.fancy:
            self.log_path = log_path
            if log_path:
                os.makedirs(os.path.dirname(log_path), exist_ok=True)
                sys.stdout.flush()
                sys.stderr.flush()
                saved = (os.dup(1), os.dup(2))
                self.term = os.fdopen(os.dup(2), "w", buffering=1, encoding="utf-8", errors="replace")
                self.term_out = os.fdopen(os.dup(1), "w", buffering=1, encoding="utf-8", errors="replace")
                fd = os.open(log_path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
                os.dup2(fd, 1)
                os.dup2(fd, 2)
                os.close(fd)
                self._saved_fds = saved
                self.follow(log_path, "")
            self._keys_on()
            self._write("\x1b[?25l")
        if self.fancy and signal.getsignal(signal.SIGINT) == signal.SIG_IGN:
            signal.signal(signal.SIGINT, signal.default_int_handler)  # the footer promises Ctrl-C stops
        atexit.register(self.stop)
        for sig in (signal.SIGTERM, signal.SIGHUP):
            try:
                if signal.getsignal(sig) in (signal.SIG_DFL, None):
                    signal.signal(sig, _raise_exit)
            except (ValueError, OSError):
                pass
        if self.fancy or self.verbose or self.command in ("fetch", "build", "run"):
            t = threading.Thread(target=self._loop, name="kickem-ui", daemon=True)
            t.start()
            self._threads.append(t)

    def stop(self):
        if not self._started:
            return
        self._started = False
        self._stop.set()
        for t in self._threads:
            t.join(timeout=1.0)
        self._threads = []
        with self.lock:
            self._pump()
            if self.fancy:
                self._clear_live()
                self._write("\x1b[?25h")
                self._keys_off()
        if self._saved_fds:
            sys.stdout.flush()
            sys.stderr.flush()
            os.dup2(self._saved_fds[0], 1)
            os.dup2(self._saved_fds[1], 2)
            for fd in self._saved_fds:
                os.close(fd)
            self._saved_fds = None
            try:
                self.term.flush()
                self.term_out.flush()
            except (OSError, ValueError):
                pass

    # --- Keys ----------------------------------------------------------------------------------
    def _keys_on(self):
        if termios is None or not _isatty(sys.stdin):
            return
        fd = sys.stdin.fileno()
        try:
            self._tio = termios.tcgetattr(fd)
            new = termios.tcgetattr(fd)
            new[3] &= ~(termios.ICANON | termios.ECHO | termios.IEXTEN)
            new[6][termios.VMIN] = 1
            new[6][termios.VTIME] = 0
            termios.tcsetattr(fd, termios.TCSANOW, new)
        except termios.error:
            self._tio = None
            return
        self.keys = True

    def _keys_off(self):
        if self._tio is not None:
            try:
                termios.tcsetattr(sys.stdin.fileno(), termios.TCSANOW, self._tio)
            except (termios.error, ValueError, OSError):
                pass
            self._tio = None
        self.keys = False

    def _read_keys(self):
        if not self.keys:
            return
        fd = sys.stdin.fileno()
        try:
            r, _, _ = select.select([fd], [], [], 0)
            if not r:
                return
            data = os.read(fd, 64)
        except (OSError, ValueError):
            return
        for k in KEY_DETAILS:
            if k in data:
                self.toggle_details()
                break

    def toggle_details(self):
        with self.lock:
            self.details = not self.details
            if self.details:
                self._emit(self._style("── details: the output of what runs underneath (ctrl-o: back to the compact view) ──", "dim"))
                st = self.steps[-1] if self.steps else None
                if st and st.log:
                    ctx = tail_text(st.log, st.log_off, 15)
                    for line in [x for x in ctx.split("\n") if x.strip()] if ctx else ():
                        self._emit(self._detail_line(line, self.followed.get(st.log, [0, ""])[1]))
            else:
                self._emit(self._style("── compact view (ctrl-o: details) ──", "dim"))

    # --- Following logs ------------------------------------------------------------------------
    def follow(self, path, label=None):
        """Streams PATH's new lines in the details view (and to its step's progress parser)."""
        with self.lock:
            if path in self.followed:
                return
            if label is None:
                label = {"kick.log": "kick", "panel.log": "panel", "boot.log": "vm"}.get(os.path.basename(path), "")
            self.followed[path] = [_size(path), label, ""]

    def unfollow(self, path):
        with self.lock:
            self.followed.pop(path, None)

    def _pump(self):
        """Reads what the followed logs gained; feeds the steps' parsers; prints it in details view."""
        for path, ent in list(self.followed.items()):
            try:
                size = os.path.getsize(path)
            except OSError:
                continue
            if size < ent[0]:
                ent[0] = 0  # truncated (a new run's log)
            if size == ent[0]:
                continue
            try:
                with open(path, "rb") as f:
                    f.seek(ent[0])
                    data = f.read(min(size - ent[0], 1 << 20))
            except OSError:
                continue
            ent[0] += len(data)
            text = (ent[2] + data.decode("utf-8", "replace")).replace("\r\n", "\n")
            lines = text.split("\n")
            partial = lines.pop()  # an unfinished line waits for the rest
            # A progress meter rewrites its line with \r: every update goes to the parser, only
            # the last one of a line is shown.
            if "\r" in partial:
                done_parts = partial.split("\r")
                partial = done_parts.pop()
                self._parse(path, done_parts)
            ent[2] = partial
            own = path == self.log_path
            for line in lines:
                segs = line.split("\r")
                self._parse(path, segs)
                line = next((x for x in reversed(segs) if x.strip()), "")
                if not line.strip():
                    continue
                if own and (line.startswith(self.prefix) or line.startswith("kickem vm: ")):
                    continue  # kickem's own lines: already on screen
                if self.details:
                    self._emit(self._detail_line(line, ent[1]))

    def _parse(self, path, lines):
        for st in list(self.steps):
            if st.log != path or not st.parse:
                continue
            for line in lines:
                if not line.strip():
                    continue
                try:
                    p = st.parse(line)
                except Exception:
                    p = None
                if p:
                    st.progress = p

    def _detail_line(self, line, label):
        line = ANSI.sub("", line).rstrip()
        if not self.fancy:
            return "  | " + (label + ": " if label else "") + line
        return self._style(("%5s │ " % label[:5]) if label else "      │ ", "dim") + line

    # --- Steps ---------------------------------------------------------------------------------
    def step(self, label, log=None, parse=None, poll=None, poll_every=1.0):
        return Step(self, label, log, parse, poll, poll_every)

    def _start(self, st):
        with self.lock:
            if st not in self.steps:
                self.steps.append(st)
            if not self.fancy:
                self._plain("[....] " + st.label)
            self._redraw()

    def _finish(self, st, status, result, msg=None, log=None, log_off=0):
        with self.lock:
            el = st.elapsed()
            if st in self.steps and status != "next":
                self.steps.remove(st)
            if self.fancy:
                mark = self._style(OK, "green") if status == "ok" else self._style(FAIL, "red")
                label = st.label if status == "ok" else self._style(st.label, "bold")
                line = "%s %s" % (mark, label)
                if result:
                    line += "  " + result
                line += "  " + self._style(fmt_time(el), "dim")
                self._emit(line)
                if msg:
                    self._emit("  " + self._style(msg, "red"))
            else:
                self._plain("[%s] %s%s  %s" % (" ok " if status == "ok" else "FAIL", st.label,
                                               ("  " + result) if result else "", fmt_time(el)))
                if msg:
                    self._plain(msg)
            if status != "ok" and log:
                self.show_tail(log, log_off)
            self._redraw()

    def show_tail(self, log, offset=0):
        text = tail_text(log, offset)
        with self.lock:
            if text:
                if self.fancy:
                    self._emit(self._style("  ── last %d lines of %s ──" % (len(text.split("\n")), log), "dim"))
                    for line in text.split("\n"):
                        self._emit(self._style("  │ ", "dim") + ANSI.sub("", line))
                else:
                    self._writeerr(text + "\n")
            if self.fancy:
                self._emit("  " + self._style("full log: ", "dim") + log)
            else:
                self._plain("full log: %s" % log)

    def line(self, mark, label, result="", elapsed=None):
        """A finished line without a running phase (for steps that were skipped or instant)."""
        with self.lock:
            if self.fancy:
                m = {"ok": self._style(OK, "green"), "skip": self._style(DOT, "dim"), "warn": self._style(WARN, "yellow"),
                     "fail": self._style(FAIL, "red")}[mark]
                s = "%s %s" % (m, label)
                if result:
                    s += "  " + (self._style(result, "dim") if mark == "skip" else result)
                if elapsed is not None:
                    s += "  " + self._style(fmt_time(elapsed), "dim")
                self._emit(s)
            else:
                self._plain("[%s] %s%s%s" % ({"ok": " ok ", "skip": "skip", "warn": "warn", "fail": "FAIL"}[mark], label,
                                             ("  " + result) if result else "",
                                             ("  " + fmt_time(elapsed)) if elapsed is not None else ""))

    # --- Messages ------------------------------------------------------------------------------
    def info(self, msg):
        """A detail: on its own line in plain mode (as kickem always printed it); in the compact
        view only in the log and in the details view."""
        with self.lock:
            if not self.fancy:
                self._plain(msg)
                return
            self._log(self.prefix + msg)
            if self.details:
                self._emit(self._style("      · " + msg, "dim"))

    def note(self, msg):
        with self.lock:
            if not self.fancy:
                self._plain(msg)
                return
            self._log(self.prefix + msg)
            self._emit(self._style("  " + msg, "dim"))

    def warn(self, msg):
        with self.lock:
            if not self.fancy:
                self._plain(msg)
                return
            self._log(self.prefix + msg)
            self._emit(self._style(WARN, "yellow") + " " + self._style(msg, "yellow"))

    def error(self, msg):
        with self.lock:
            if not self.fancy:
                self._plain(msg)
                return
            self._log(self.prefix + msg)
            self._emit(self._style(FAIL, "red") + " " + self._style(msg, "red"))

    def out(self, text, show=True):
        """A line for stdout (scripts read these). In the compact view on a terminal it is shown
        dim when SHOW, and always written to the log."""
        with self.lock:
            if not self.fancy:
                print(text, flush=True)
                return
            self._log(self.prefix + text)
            if not self.stdout_tty:
                self.term_out.write(text + "\n")
                self.term_out.flush()
            elif show:
                self._emit(self._style("  " + text, "dim"))

    def style(self, text, *names):
        return self._style(text, *names)

    # --- Drawing -------------------------------------------------------------------------------
    def _style(self, text, *names):
        if not self.color or not names:
            return text
        return "\x1b[%sm%s\x1b[0m" % (";".join(STYLES[n] for n in names), text)

    def _plain(self, msg):
        self._writeerr(self.prefix + msg + "\n")

    def _writeerr(self, text):
        try:
            sys.stderr.write(text)
            sys.stderr.flush()
        except (OSError, ValueError):
            pass

    def _log(self, text):
        if self.log_path:
            try:
                sys.stderr.write(ANSI.sub("", text) + "\n")
                sys.stderr.flush()
            except (OSError, ValueError):
                pass

    def _write(self, text):
        try:
            self.term.write(text)
            self.term.flush()
        except (OSError, ValueError):
            pass

    def _width(self):
        try:
            return max(20, os.get_terminal_size(self.term.fileno()).columns)
        except (OSError, ValueError, AttributeError):
            return 80

    def _emit(self, line):
        """A permanent line above the live region."""
        if not self.fancy:
            self._writeerr(ANSI.sub("", line) + "\n")
            return
        self._write(self._clear_seq() + line + "\n" + self._draw_seq())

    def _clear_seq(self):
        seq = ""
        if self.live:
            seq = "\r" + ("\x1b[%dA" % (self.live - 1) if self.live > 1 else "") + "\x1b[J"
            self.live = 0
        return seq

    def _clear_live(self):
        self._write(self._clear_seq())

    def _live_lines(self):
        lines = []
        w = self._width() - 1
        for st in self.steps:
            spin = self._style(SPINNER[self._frame % len(SPINNER)], "cyan")
            text = st.label
            prog = ("  " + st.progress) if st.progress else ""
            el = "  " + fmt_time(st.elapsed())
            room = w - 2 - len(text) - len(el)
            if room < len(prog):
                prog = prog[:max(0, room - 1)] + ("…" if room > 1 else "")
            text = (text + prog)[: max(0, w - 2 - len(el))]
            lines.append("%s %s%s" % (spin, text, self._style(el, "dim")))
        if self.footer and (self.steps or self.keys):
            if self.keys:
                f = ("ctrl-o: compact view" if self.details else "ctrl-o: details") + "  ·  ctrl-c: stop"
            else:
                f = "ctrl-c: stop"
            lines.append(self._style(f[:w], "dim"))
        return lines

    def _draw_seq(self):
        if not self._started:
            return ""
        lines = self._live_lines()
        self.live = len(lines)
        return ("\r" + "\n".join("\x1b[2K" + l for l in lines)) if lines else ""

    def _redraw(self):
        if self.fancy and self._started:
            self._write(self._clear_seq() + self._draw_seq())

    def _loop(self):
        while not self._stop.wait(0.1):
            with self.lock:
                self._frame += 1
                self._read_keys()
                self._pump()
                now = time.monotonic()
                for st in list(self.steps):
                    if st.poll and now >= st.next_poll:
                        st.next_poll = now + st.poll_every
                        try:
                            p = st.poll()
                        except Exception:
                            p = None
                        if p is not None:
                            st.progress = p
                    if not self.fancy and st.progress and now - st.plain_last[0] >= 30 and st.progress != st.plain_last[1]:
                        st.plain_last = (now, st.progress)
                        self._plain("[....] %s  %s  %s" % (st.label, st.progress, fmt_time(st.elapsed())))
                self._redraw()


def _isatty(f):
    try:
        return f.isatty()
    except (AttributeError, ValueError):
        return False


def _raise_exit(signum, frame):
    raise SystemExit(128 + signum)
