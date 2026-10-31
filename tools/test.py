#!/usr/bin/env python3
"""
GazOS end-to-end test: boot disk.img in QEMU, type shell commands, and
check the output. Runs every combination of firmware (UEFI, legacy BIOS)
and keyboard (USB keyboard on xHCI with no PS/2 controller at all; PS/2
keyboard with no USB).

The kernel mirrors all console output to COM1; QEMU writes that to a log
file which this script reads. Keystrokes are sent with the QEMU monitor's
"sendkey" command, exactly as if typed on the PS/2 keyboard. The disk is
opened with -snapshot, so tests never modify disk.img.

usage: tools/test.py [--firmware uefi|bios|all] [--keyboard usb|ps2|all]
                     [--disk disk.img] [--src DIR] [--headless] [--keep]
"""

import argparse
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PROMPT = "GazOS] "
MORE = "-- more (any key) --"

# characters -> QEMU key names (US layout)
KEYS = {" ": "spc", "\n": "ret", "-": "minus", "=": "equal", "[": "bracket_left",
        "]": "bracket_right", "\\": "backslash", ";": "semicolon",
        "'": "apostrophe", "`": "grave_accent", ",": "comma", ".": "dot",
        "/": "slash"}
SHIFTED = {"!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6", "&": "7",
           "*": "8", "(": "9", ")": "0", "_": "minus", "+": "equal",
           "{": "bracket_left", "}": "bracket_right", "|": "backslash",
           ":": "semicolon", '"': "apostrophe", "~": "grave_accent",
           "<": "comma", ">": "dot", "?": "slash"}


class GazOS:
    def __init__(self, mode, keyboard, disk, headless, workdir):
        self.log_path = os.path.join(workdir, f"serial-{mode}-{keyboard}.log")
        self.mon_path = os.path.join(workdir, f"monitor-{mode}-{keyboard}.sock")
        # same machine/firmware setup as ./run.sh (private OVMF vars copy)
        base = subprocess.run(
            [os.path.join(HERE, "tools", "qemu-args.sh"), mode, disk,
             os.path.join(workdir, "ovmf_vars.fd")],
            check=True, capture_output=True, text=True,
            env=dict(os.environ, KEYBOARD=keyboard)).stdout.split()
        cmd = base + ["-snapshot",
                      "-serial", f"file:{self.log_path}",
                      "-monitor", f"unix:{self.mon_path},server,nowait"]
        cmd[cmd.index("-name") + 1] = f"GazOS-test-{mode}-{keyboard}"
        if headless:
            cmd += ["-display", "none"]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.PIPE)
        deadline = time.time() + 10
        while not os.path.exists(self.mon_path):
            if self.proc.poll() is not None or time.time() > deadline:
                raise RuntimeError("QEMU failed to start: " +
                                   self.proc.stderr.read().decode(errors="replace"))
            time.sleep(0.1)
        self.mon = socket.socket(socket.AF_UNIX)
        self.mon.connect(self.mon_path)

    def log(self):
        try:
            with open(self.log_path, "rb") as f:
                return f.read().decode("latin-1")
        except FileNotFoundError:
            return ""

    def key(self, name):
        self.mon.sendall(f"sendkey {name}\n".encode())
        time.sleep(0.03)

    def type(self, text):
        for ch in text:
            if ch in KEYS:
                self.key(KEYS[ch])
            elif ch in SHIFTED:
                self.key("shift-" + SHIFTED[ch])
            elif ch.isupper():
                self.key("shift-" + ch.lower())
            elif ch.isalnum():
                self.key(ch)
            else:
                raise ValueError(f"don't know how to type {ch!r}")

    def wait_prompt(self, start, timeout=20):
        """Wait for the next shell prompt after log offset 'start', paging
        through any "-- more --" prompts. Returns the log text since start."""
        deadline = time.time() + timeout
        mores = 0
        while time.time() < deadline:
            out = self.log()[start:]
            if out.count(MORE) > mores:
                mores = out.count(MORE)
                self.key("spc")
                deadline = time.time() + timeout
            # prompt is "GazOS] <cwd>: "
            i = out.rfind(PROMPT)
            if i >= 0 and out.endswith(": ") and "\n" not in out[i:]:
                return out
            time.sleep(0.1)
        raise TimeoutError("no shell prompt; output so far:\n" + self.log()[start:])

    def run(self, command, timeout=20):
        """Type a command; return its output (without the echo and prompt)."""
        start = len(self.log())
        self.type(command + "\n")
        out = self.wait_prompt(start, timeout)
        out = out.replace("\r\n", "\n")
        out = out[out.index("\n") + 1:]            # drop the echoed command
        return out[:out.rfind("\n" + PROMPT)]       # drop the next prompt

    def close(self):
        try:
            self.mon.sendall(b"quit\n")
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()


def run_tests(mode, keyboard, args, workdir):
    """Boot once and run every test. Returns the failure count."""
    failures = 0
    print(f"=== booting via {mode.upper()}, {keyboard.upper()} keyboard ===")
    os_ = GazOS(mode, keyboard, os.path.abspath(args.disk), args.headless, workdir)

    def check(name, ok, detail=""):
        nonlocal failures
        print(("PASS  " if ok else "FAIL  ") + name)
        if not ok:
            failures += 1
            if detail:
                print("      " + detail.replace("\n", "\n      "))

    def expect(command, *wanted, name=None):
        try:
            out = os_.run(command)
        except TimeoutError as e:
            check(name or command, False, str(e))
            return ""
        missing = [w for w in wanted if w not in out]
        check(name or command, not missing,
              f"missing {missing!r} in output:\n{out}" if missing else "")
        return out

    try:
        # --- boot ---
        try:
            boot = os_.wait_prompt(0, timeout=60)
        except TimeoutError as e:
            check("boot to shell prompt", False, str(e))
            return failures
        check("boot to shell prompt", True)
        banner = "Booted via UEFI" if mode == "uefi" else "Booted via legacy BIOS"
        check(f"kernel reports '{banner}'", banner in boot, boot)
        if mode == "uefi":
            check("UEFI framebuffer console", "framebuffer" in boot, boot)
            check("memory size from the UEFI memory map", "Mem.Total = " in boot, boot)
        if keyboard == "usb":
            check("no PS/2 controller detected (and no hang)",
                  "No PS/2 controller" in boot, boot)
            check("xHCI controller found", "Initialising USB (xHCI)... controller" in boot, boot)
            check("USB keyboard enumerated", "USB keyboard ready" in boot, boot)
        else:
            check("no xHCI controller (and no hang)", "no xHCI controller" in boot, boot)
        check("AHCI finds the SATA disk", "SATA disk on port" in boot, boot)
        check("FAT32 volume mounted", 'volume "GAZOS"' in boot, boot)

        # --- shell + filesystem ---
        out = expect("ls", "KERNEL.ELF", "EFI/", "docs/", "src/", "help.txt", "readme.txt",
                     name="ls lists the root directory (incl. KERNEL.ELF, EFI/)")
        check("firmware didn't write NvVars to the disk", "NvVars" not in out, out)
        expect("ls /efi/boot", "BOOTX64.EFI", name="UEFI loader is at /EFI/BOOT/BOOTX64.EFI")
        expect("cat help.txt", "GazOS Helpfile", name="cat a file")
        expect("type readme.txt", "GazOS Readme", name="type alias")
        os_.run("cd docs")
        expect("pwd", "/docs", name="cd into a directory")
        expect("cat a file with a long name.txt", "LFN support works",
               name="long file name lookup")
        expect("ls /DOCS/Nested", "deep.txt", name="case-insensitive absolute path")
        os_.run("cd nested")
        expect("cat ../../docs/nested/deep.txt", "Hello from /docs/nested/deep.txt",
               name="relative path with ..")
        os_.run("cd ../..")
        out = os_.run("pwd")
        check("cd ../.. back to /", out.strip() == "/", out)
        out = os_.run("   pwd")
        check("leading spaces ignored", out.strip() == "/", out)

        # --- multi-cluster read, byte for byte ---
        src = os.path.join(args.src, "fat32.c")
        with open(src, encoding="latin-1") as f:
            want = f.read()
        try:
            got = os_.run("cat /src/fat32.c", timeout=30)
            got = got.replace(MORE + "\n", "")
            ok = got.rstrip("\n") == want.rstrip("\n")
            detail = ""
            if not ok:
                n = next((i for i, (a, b) in enumerate(zip(got, want)) if a != b),
                         min(len(got), len(want)))
                detail = (f"differs at byte {n} (got {len(got)} bytes, want "
                          f"{len(want)}):\n got: {got[n:n+60]!r}\nwant: {want[n:n+60]!r}")
            check(f"cat /src/fat32.c matches the source ({len(want)} bytes)", ok, detail)
        except TimeoutError as e:
            check("cat /src/fat32.c", False, str(e))

        # --- errors ---
        expect("cat /no/such/file", "not found", name="missing file reports an error")
        expect("cd help.txt", "no such directory", name="cd into a file fails")
        expect("frobnicate", "unknown command", name="unknown command")
        symbols = "x!@#$%^&*()_+{}|:\"<>?-=[]\\;',./`~"
        expect(symbols, "unknown command: " + symbols,
               name="every shifted/unshifted symbol key types correctly")
        expect("disk", "QEMU HARDDISK", "FAT32 at LBA 2048", name="disk info")
    finally:
        os_.close()
    return failures


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--firmware", choices=["uefi", "bios", "all"], default="all",
                    help="boot mode(s) to test (default: all)")
    ap.add_argument("--keyboard", choices=["usb", "ps2", "all"], default="all",
                    help="keyboard(s) to test with (default: all)")
    ap.add_argument("--disk", default=os.path.join(HERE, "disk.img"))
    ap.add_argument("--src", default=HERE, help="source dir (for /src checks)")
    ap.add_argument("--headless", action="store_true", help="no QEMU window")
    ap.add_argument("--keep", action="store_true", help="keep the serial logs")
    args = ap.parse_args()

    if not os.path.exists(args.disk):
        sys.exit(f"{args.disk} not found: run ./build.sh first")

    modes = ["uefi", "bios"] if args.firmware == "all" else [args.firmware]
    keyboards = ["usb", "ps2"] if args.keyboard == "all" else [args.keyboard]
    workdir = tempfile.mkdtemp(prefix="gazos-test-")
    results = {}
    for mode in modes:
        for keyboard in keyboards:
            results[(mode, keyboard)] = run_tests(mode, keyboard, args, workdir)
            print()

    failed = sum(results.values())
    for (mode, keyboard), n in results.items():
        label = f"{mode.upper()} + {keyboard.upper()}"
        print(f"{label:11} {'all tests passed' if not n else f'{n} test(s) FAILED'}")
    if args.keep or failed:
        print(f"serial logs: {workdir}")
    else:
        shutil.rmtree(workdir, ignore_errors=True)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
