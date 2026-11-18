#!/usr/bin/env python3
"""REDLINE Flasher - flash a REDLINE release onto a CYD board, no PlatformIO needed.

Windows / macOS / Linux. A small window (or `--cli` in a terminal) that:
  * lists serial ports and pre-selects the CYD's USB chip (CH340, else CP210x / other ESP32 bridges)
  * downloads the chosen version from GitHub Releases, or uses a local .bin you pick
  * always flashes a factory / merged image at 0x0, resetting settings and the run log

    python redline_flasher.py                       # window
    python redline_flasher.py --cli                 # interactive terminal prompts
    python redline_flasher.py --cli --port COM5 --variant noinvert --spi 40 --version latest -y
"""
import argparse
import json
import os
import sys
import tempfile
import threading
import urllib.request

REPO = "moomdate/redline-gauge"
API = f"https://api.github.com/repos/{REPO}/releases"
# Panel colours only set the starting point: from v1.6.2 SETUP -> COLORS flips them on the
# board (saved), so a wrong pick is fixed with one tap. The -dim builds add BRIGHTNESS.
VARIANTS = {
    "noinvert": "ILI9341: TPM408-2.8 / H1954 5-4 (default)",
    "cyd-st7789": "ST7789: H685",
    "invert": "ILI9341: only if colours look negative; SETUP -> COLORS also fixes this",
    "noinvert-dim": "ILI9341: TPM408-2.8 / H1954 5-4 + BRIGHTNESS",
    "invert-dim": "ILI9341 + BRIGHTNESS: only for negative colours (or SETUP -> COLORS)",
}
# 40 MHz is the safe starting point; some panels distort at 80 MHz.
SPEEDS = {
    "40": "40 MHz - safe (default)",
    "80": "80 MHz - faster bus; may distort the picture",
}
IMAGE_AVAILABILITY = (
    "invert-dim from v1.3.0, noinvert-dim from v1.4.1, ILI9341 40 MHz from v1.6.5; "
    "ST7789 from v1.9.1, ST7789 40 MHz from v1.9.2 (display fix in v1.9.3)"
)

# USB-serial chips found on ESP32 boards, best guess first. The CYD uses a CH340.
KNOWN_USB = [
    ((0x1A86, 0x7523), "CH340 (CYD)"),
    ((0x1A86, 0x55D4), "CH9102"),
    ((0x10C4, 0xEA60), "CP210x"),
    ((0x0403, 0x6001), "FTDI"),
    ((0x303A, None), "ESP32 native USB"),
]


# ---- ports --------------------------------------------------------------------------------
def port_rank(p):
    for i, ((vid, pid), _) in enumerate(KNOWN_USB):
        if p.vid == vid and (pid is None or p.pid == pid):
            return i
    return len(KNOWN_USB) if p.vid else len(KNOWN_USB) + 1   # unknown USB, then Bluetooth etc.


def port_label(p):
    chip = next((name for (vid, pid), name in KNOWN_USB if p.vid == vid and (pid is None or p.pid == pid)), None)
    desc = chip or (p.description if p.description and p.description != "n/a" else "")
    return f"{p.device}  -  {desc}" if desc else p.device


def list_ports():
    """Serial ports, the most likely ESP32 board first."""
    import serial.tools.list_ports
    ports = [p for p in serial.tools.list_ports.comports()
             if "Bluetooth" not in p.device and "debug-console" not in p.device and "wlan" not in p.device]
    usb = [p for p in ports if p.vid]          # Bluetooth / virtual ports have no USB id
    return sorted(usb or ports, key=lambda p: (port_rank(p), p.device))


# ---- releases ------------------------------------------------------------------------------
def fetch_releases():
    req = urllib.request.Request(API, headers={"Accept": "application/vnd.github+json",
                                               "User-Agent": "redline-flasher"})
    with urllib.request.urlopen(req, timeout=15) as r:
        rels = json.load(r)
    return [r for r in rels if not r.get("draft")]


def asset_for(release, variant, spi="40"):
    tag = release["tag_name"]
    name = f"redline-{tag}-{variant}{'-spi40' if spi == '40' else ''}-factory.bin"
    for a in release.get("assets", []):
        if a["name"] == name:
            return a
    return None


def download(url, log):
    dest = os.path.join(tempfile.gettempdir(), url.rsplit("/", 1)[-1])
    log(f"Downloading {url.rsplit('/', 1)[-1]} ...")
    req = urllib.request.Request(url, headers={"User-Agent": "redline-flasher"})
    with urllib.request.urlopen(req, timeout=60) as r, open(dest, "wb") as f:
        f.write(r.read())
    log(f"  {os.path.getsize(dest) // 1024} KB")
    return dest


# ---- flashing --------------------------------------------------------------------------------
def validate_factory_image(path):
    """Require the ESP32 merged layout at 0x0, regardless of the filename."""
    with open(path, "rb") as f:
        header = f.read(0x10001)
    if (header[:1] == b"\xe9" or header[0x1000:0x1001] != b"\xe9"
            or header[0x8000:0x8002] != b"\xaa\x50"
            or header[0x10000:0x10001] != b"\xe9"):
        raise ValueError(
            "Use a -factory.bin or merged ESP32 image built for 0x0, containing the "
            "bootloader, partition table and app. App-only / -update.bin images are not "
            "supported: they can leave the stock 1 MB partition table and cause a "
            "black-screen boot loop. Renaming the file does not convert it."
        )


def flash(port, path, log, baud=460800):
    validate_factory_image(path)
    import esptool
    addr = "0x0"
    log("Factory flash at 0x0: resets settings and the run log.")
    args = ["--chip", "esp32", "--port", port, "--baud", str(baud), "--before", "default_reset",
            "--after", "hard_reset", "write_flash", "-z", addr, path]
    log(f"esptool {' '.join(args)}")

    class Tee:                                   # esptool prints progress; forward it to the log
        def write(self, s):
            for line in s.replace("\r", "\n").split("\n"):
                if line.strip():
                    log(line.rstrip())

        def flush(self):
            pass

    if log is print:                             # terminal: esptool can print directly
        esptool.main(args)
    else:
        old = sys.stdout
        sys.stdout = Tee()
        try:
            esptool.main(args)
        finally:
            sys.stdout = old
    log("Done - the board restarts by itself.")


# ---- terminal mode -------------------------------------------------------------------------------
def pick(prompt, options, default=0):
    for i, o in enumerate(options):
        print(f"  {i + 1}) {o}{'   <- default' if i == default else ''}")
    s = input(f"{prompt} [{default + 1}]: ").strip()
    return int(s) - 1 if s.isdigit() and 1 <= int(s) <= len(options) else default


def cli(a):
    log = print
    ports = list_ports()
    if a.port:
        port = a.port
    elif not ports:
        sys.exit("No serial port found. Plug in the board (and install the CH340 driver on Windows).")
    elif a.yes:
        port = ports[0].device
    else:
        print("Serial port:")
        port = ports[pick("Port", [port_label(p) for p in ports])].device

    variants = list(VARIANTS)
    variant = a.variant or (variants[0] if a.yes else
                            variants[pick("Panel type", [f"{k:11s} {v}" for k, v in VARIANTS.items()])])
    speeds = list(SPEEDS)
    spi = a.spi or (speeds[0] if a.yes else speeds[pick("Display speed", list(SPEEDS.values()))])

    if a.file:
        path = a.file
    else:
        rels = fetch_releases()
        if not rels:
            sys.exit("No releases found on GitHub.")
        if a.version and a.version != "latest":
            want = a.version if a.version.startswith("v") else "v" + a.version
            rel = next((r for r in rels if r["tag_name"] == want), None)
            if not rel:
                sys.exit(f"Version {want} not found. Available: {', '.join(r['tag_name'] for r in rels)}")
        elif a.version == "latest" or a.yes:
            rel = rels[0]
        else:
            print("Version:")
            rel = rels[pick("Version", [r["tag_name"] for r in rels])]
        asset = asset_for(rel, variant, spi)
        if not asset:
            sys.exit(f"{rel['tag_name']} has no '{variant}' {spi} MHz factory image ({IMAGE_AVAILABILITY}).")
        path = download(asset["browser_download_url"], log)

    try:
        validate_factory_image(path)
    except (OSError, ValueError) as e:
        sys.exit(str(e))
    print(f"\nFlash {os.path.basename(path)} to {port} at 0x0. Resets settings and the run log.")
    if not a.yes and input("Go? [Y/n]: ").strip().lower() not in ("", "y", "yes"):
        sys.exit("Cancelled.")
    flash(port, path, log, a.baud)


# ---- window ------------------------------------------------------------------------------------
def gui(a):
    import tkinter as tk
    from tkinter import filedialog, ttk

    if sys.platform == "win32":                    # crisp text on high-DPI Windows screens
        try:
            import ctypes
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except Exception:
            pass
    root = tk.Tk()
    root.title("REDLINE Flasher - birdlab.th")
    root.minsize(560, 560)
    try:                                           # birdlab.th logo: window icon + header
        from assets import ICON_GIF, BANNER_GIF
        icon = tk.PhotoImage(data=ICON_GIF)
        root.iconphoto(True, icon)
        head = tk.Frame(root, bg="#12161c")
        head.pack(fill="x")
        banner = tk.PhotoImage(data=BANNER_GIF)
        tk.Label(head, image=banner, bg="#12161c").pack(side="left", padx=4, pady=2)
        tk.Label(head, text="REDLINE Flasher", fg="#ffffff", bg="#12161c",
                 font=("Segoe UI", 13, "bold")).pack(side="right", padx=12)
        root._logo_refs = (icon, banner)           # keep the images alive
    except Exception:
        pass
    frm = ttk.Frame(root, padding=12)
    frm.pack(fill="both", expand=True)
    frm.columnconfigure(1, weight=1)

    ports, releases = [], []
    port_var, ver_var, var_var, file_var, spi_var = (tk.StringVar() for _ in range(5))
    var_var.set(a.variant or "noinvert")
    spi_var.set(a.spi or "40")

    def log(msg):
        def add():
            out.insert("end", msg + "\n")
            out.see("end")
        root.after(0, add)

    def refresh_ports():
        ports[:] = list_ports()
        labels = [port_label(p) for p in ports]
        port_box["values"] = labels
        port_var.set(labels[0] if labels else "")
        if not labels:
            log("No serial port found - plug in the board (Windows: install the CH340 driver).")

    def load_releases():
        try:
            releases[:] = fetch_releases()
        except Exception as e:                     # offline: local file still works
            log(f"Could not reach GitHub ({e}). Use 'Local file'.")
            return
        # The list is read from GitHub Releases every time the app starts, so a new
        # firmware shows up here without updating the flasher.
        tags = [r["tag_name"] + ("  (latest)" if i == 0 else "") for i, r in enumerate(releases)]
        root.after(0, lambda: (ver_box.configure(values=tags), ver_var.set(tags[0] if tags else "")))
        log(f"Releases: {', '.join(tags)}  (latest: {tags[0] if tags else '-'})")

    def choose_file():
        p = filedialog.askopenfilename(filetypes=[("Firmware", "*.bin"), ("All files", "*.*")])
        if p:
            file_var.set(p)

    def start():
        sel = port_box.current()
        if sel < 0 and not port_var.get():
            log("Pick a serial port first.")
            return
        port = ports[sel].device if sel >= 0 else port_var.get().split()[0]
        go.configure(state="disabled")

        def work():
            try:
                path = file_var.get()
                if not path:
                    tag = ver_var.get().split()[0] if ver_var.get() else ""
                    rel = next((r for r in releases if r["tag_name"] == tag), None)
                    if not rel:
                        raise RuntimeError("Pick a version (or a local .bin).")
                    asset = asset_for(rel, var_var.get(), spi_var.get())
                    if not asset:
                        raise RuntimeError(f"{rel['tag_name']} has no '{var_var.get()}' {spi_var.get()} MHz image "
                                           f"({IMAGE_AVAILABILITY}).")
                    path = download(asset["browser_download_url"], log)
                flash(port, path, log, a.baud)
            except SystemExit as e:                # esptool exits on fatal errors
                log(f"Failed ({e}). Check the port; hold BOOT while it connects if it keeps failing.")
            except Exception as e:
                log(f"Failed: {e}")
            finally:
                root.after(0, lambda: go.configure(state="normal"))
        threading.Thread(target=work, daemon=True).start()

    r = 0
    ttk.Label(frm, text="Port").grid(row=r, column=0, sticky="w", pady=3)
    port_box = ttk.Combobox(frm, textvariable=port_var, state="readonly")
    port_box.grid(row=r, column=1, sticky="ew", padx=6)
    ttk.Button(frm, text="Refresh", command=refresh_ports).grid(row=r, column=2)
    r += 1
    ttk.Label(frm, text="Version").grid(row=r, column=0, sticky="w", pady=3)
    ver_box = ttk.Combobox(frm, textvariable=ver_var, state="readonly")
    ver_box.grid(row=r, column=1, sticky="ew", padx=6)
    r += 1
    ttk.Label(frm, text="Panel").grid(row=r, column=0, sticky="nw", pady=3)
    pf = ttk.Frame(frm)
    pf.grid(row=r, column=1, columnspan=2, sticky="w", padx=6)
    for k, v in VARIANTS.items():
        ttk.Radiobutton(pf, text=f"{k}  -  {v}", value=k, variable=var_var).pack(anchor="w")
    r += 1
    ttk.Label(frm, text="Display").grid(row=r, column=0, sticky="nw", pady=3)
    sf = ttk.Frame(frm)
    sf.grid(row=r, column=1, columnspan=2, sticky="w", padx=6)
    for k, v in SPEEDS.items():
        ttk.Radiobutton(sf, text=v, value=k, variable=spi_var).pack(anchor="w")
    r += 1
    ttk.Label(frm, text="Factory at 0x0 — resets settings and the run log.").grid(
        row=r, column=0, columnspan=3, sticky="w", pady=3)
    r += 1
    ttk.Label(frm, text="Local file").grid(row=r, column=0, sticky="w", pady=3)
    ttk.Entry(frm, textvariable=file_var).grid(row=r, column=1, sticky="ew", padx=6)
    ttk.Button(frm, text="Browse...", command=choose_file).grid(row=r, column=2)
    r += 1
    go = ttk.Button(frm, text="Flash", command=start)
    go.grid(row=r, column=0, columnspan=3, sticky="ew", pady=8)
    r += 1
    out = tk.Text(frm, height=12, wrap="word")
    out.grid(row=r, column=0, columnspan=3, sticky="nsew")
    frm.rowconfigure(r, weight=1)

    refresh_ports()
    log("Match the panel marking; start at 40 MHz. Negative colours? Tap SETUP -> COLORS (v1.6.2+).")
    if a.selftest:                                  # CI: build the window, then close it
        root.after(1500, root.destroy)
    else:
        threading.Thread(target=load_releases, daemon=True).start()
    root.mainloop()


def main():
    ap = argparse.ArgumentParser(description="Flash REDLINE factory images at 0x0. Resets settings and the run log.")
    ap.add_argument("--cli", action="store_true", help="terminal prompts instead of a window")
    ap.add_argument("--port", help="serial port, e.g. COM5 or /dev/ttyUSB0 (default: auto)")
    ap.add_argument("--variant", choices=list(VARIANTS), help="panel variant (default: noinvert)")
    ap.add_argument("--mode", choices=["factory", "update"],
                    help="deprecated, ignored: update is gone; always factory at 0x0")
    ap.add_argument("--spi", choices=list(SPEEDS), help="display bus MHz: 40 (default, safe) or 80")
    ap.add_argument("--version", help="release tag, e.g. v1.4.0, or 'latest'")
    ap.add_argument("--file", help="local factory / merged .bin for 0x0 (app-only images refused)")
    ap.add_argument("--baud", type=int, default=460800)
    ap.add_argument("--list-ports", action="store_true", help="print serial ports and exit")
    ap.add_argument("-y", "--yes", action="store_true", help="no questions: auto port, defaults")
    ap.add_argument("--selftest", action="store_true", help=argparse.SUPPRESS)
    a = ap.parse_args()
    if a.mode:
        print("Note: --mode is ignored; update is gone. Always factory at 0x0; resets settings and the run log.")
    if a.selftest:
        import esptool                              # bundled? (PyInstaller check)
        print("esptool", esptool.__version__, "| ports:", [p.device for p in list_ports()])
    if a.list_ports:
        for p in list_ports():
            print(port_label(p))
        return
    if a.cli or a.yes or a.port or a.file:
        cli(a)
    else:
        try:
            gui(a)
        except ImportError:                        # no tkinter (some Linux pythons)
            cli(a)


if __name__ == "__main__":
    main()
