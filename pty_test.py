import os, pty, select, time, signal, sys, re

BIN = sys.argv[1]

def run(steps, sig=None):
    pid, fd = pty.fork()
    if pid == 0:
        os.execv(BIN, [BIN])
    buf = b""
    def drain(t=0.15):
        nonlocal buf
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try:
                    d = os.read(fd, 65536)
                except OSError:
                    return
                if not d: return
                buf += d
    drain(0.3)
    for s in steps:
        if isinstance(s, float):
            drain(s); continue
        os.write(fd, s if isinstance(s, bytes) else s.encode())
        drain(0.12)
    if sig:
        os.kill(pid, sig)
    drain(0.6)
    # wait for exit
    status = None
    deadline = time.time() + 6
    while time.time() < deadline:
        p, st = os.waitpid(pid, os.WNOHANG)
        if p:
            status = st; break
        drain(0.1); time.sleep(0.1)
    if status is None:
        os.kill(pid, signal.SIGKILL); os.waitpid(pid, 0)
        return buf.decode(errors="replace"), "TIMEOUT"
    return buf.decode(errors="replace"), (os.WEXITSTATUS(status) if os.WIFEXITED(status) else "sig%d" % os.WTERMSIG(status))

def check(name, cond, extra=""):
    print(("PASS " if cond else "FAIL ") + name + (" " + extra if extra and not cond else ""))
    return cond

ok = True
RESTORE = "\x1b[?25h\x1b[?1049l"
UP, DOWN = b"\x1b[A", b"\x1b[B"

# ---- Test 1: full scripted session ----
steps = [
    "s", 0.4, "l", 0.2, "l", 0.2, "u", "l", "l",
    "v", DOWN, UP, "j", "k", "x",            # lap list, scroll, any key back
    "z",                                       # unknown key
    "s", "s",                                  # pause, resume
    "a", "n", "07:30:00", "\n",                # add alarm
    "n", "99:99", "\n", b"\x1b",               # invalid time, then Esc cancels
    "n", "12:00", "\n",                        # HH:MM accepted
    "e", DOWN, "d", "n",                       # toggle, move, delete -> cancel
    "d", "y",                                  # delete -> confirm
    "q",
    "b", "s", 0.3, "p", DOWN, "s", "x", "q",   # budgets
    "f", "1", "demo", "\n",                    # save session
    "1", "demo", "\n", "n",                    # save again -> overwrite -> no
    "1", "demo", "\n", "y",                    # overwrite -> yes
    "3", "demo", "\n", "3", "demo", "\n", "y", # csv + overwrite
    "2", "nosuch", "\n", "2", "demo", "\n",    # load missing / existing
    "1", "bad name!", "\n", b"\x1b",           # invalid chars filtered -> 'badname' saved? -> Esc cancels
    "q",
    "r", "n", "r", "y",                        # reset cancel / confirm
    b"\x1b[3~", "h", "x", "q", "y",            # unsupported sequence, help, quit
]
out, code = run(steps)
ok &= check("t1 exit code 0", code == 0, str(code))
ok &= check("t1 terminal restore sequence", RESTORE in out)
ok &= check("t1 closing message", "stopwatch closed" in out)
ok &= check("t1 time format shown", re.search(r"\d\d:\d\d:\d\d\.\d\d\d", out) is not None)
ok &= check("t1 lap compare shown", "faster" in out or "slower" in out)
ok &= check("t1 unknown key message", "unknown key 'z'" in out)
ok &= check("t1 alarm added", "alarm 07:30:00 added" in out and "alarm 12:00:00 added" in out)
ok &= check("t1 invalid time rejected", "invalid time" in out)
ok &= check("t1 alarm deleted", "alarm deleted" in out)
ok &= check("t1 cancelled message", "cancelled" in out)
ok &= check("t1 budget started", "Coding budget started" in out)
ok &= check("t1 overwrite prompt", "Overwrite?" in out)
ok &= check("t1 session saved", "session 'demo' saved" in out)
ok &= check("t1 csv exported", "laps exported" in out)
ok &= check("t1 missing session msg", "no session named 'nosuch'" in out)
ok &= check("t1 stopwatch reset", "stopwatch reset" in out)
ok &= check("t1 files exist", os.path.exists("data/sessions/demo.txt") and os.path.exists("data/demo_laps.csv"))
if os.path.exists("data/demo_laps.csv"):
    print(open("data/demo_laps.csv").read())

# ---- Test 2: Ctrl+C while running ----
out, code = run(["s", 0.3, b"\x03"])
ok &= check("t2 ctrl-c exit 0", code == 0, str(code))
ok &= check("t2 ctrl-c restores terminal", RESTORE in out)

# ---- Test 3: SIGTERM while a prompt is open ----
out, code = run(["f", "1", "ab"], sig=signal.SIGTERM)
ok &= check("t3 sigterm exit 0", code == 0, str(code))
ok &= check("t3 sigterm restores terminal", RESTORE in out)

# ---- Test 4: lone Esc, arrows in main screen, EOF-style close ----
out, code = run([b"\x1b", UP, DOWN, "q", "n", "q", "y"])
ok &= check("t4 exit 0", code == 0, str(code))

print("\nALL PASS" if ok else "\nSOME FAILED")
