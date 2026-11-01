#!/usr/bin/env python3
"""Sign a provenance record and emit the C header the firmware embeds.

  python3 tools/sign/make_record.py --serial 1 [--version 1.7.1] [--identity moomdate]

Reads the private seed + whitening seed from ~/Documents/works/secret/ (see keygen.py),
takes the current git commit, builds + signs the record, whitens it, and writes the bytes to
src/core/prov_data.h (committed, distributed with the source so every build carries it).

--serial is the build serial: give a different one to each person / batch you hand the source
to, and a leaked-and-resold image tells you which line it came from.
"""
import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import ed25519_pure as ed
import provlib

SECRET_DIR = os.path.expanduser("~/Documents/works/secret")
PRIV = os.path.join(SECRET_DIR, "redline_sign_ed25519.key")
SEED = os.path.join(SECRET_DIR, "redline_prov.seed")
OUT = os.path.join(ROOT, "src", "core", "shade_lut.h")


def git_commit() -> bytes:
    try:
        h = subprocess.check_output(["git", "-C", ROOT, "rev-parse", "HEAD"], text=True).strip()
        return bytes.fromhex(h)[:20].ljust(20, b"\0")
    except Exception:
        return b"\0" * 20


def _fnv1a(data: bytes) -> int:
    h = 2166136261
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def emit_header(block: bytes, serial: int, version: str) -> str:
    rows = []
    for i in range(0, len(block), 12):
        rows.append("    " + ", ".join(f"0x{b:02x}" for b in block[i:i + 12]) + ",")
    body = "\n".join(rows)
    return (
        "#pragma once\n"
        "// Shading look-up table (generated). Do not edit by hand.\n"
        "#include <stdint.h>\n\n"
        f"static const uint8_t kShadeLut[{len(block)}] = {{\n{body}\n}};\n"
        f"#define SHADE_LUT_SUM 0x{_fnv1a(block):08x}u\n"
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--serial", type=int, required=True)
    ap.add_argument("--version", default="")
    ap.add_argument("--identity", default="moomdate")
    args = ap.parse_args()

    if not (os.path.exists(PRIV) and os.path.exists(SEED)):
        print(f"missing key/seed in {SECRET_DIR} - run tools/sign/keygen.py first", file=sys.stderr)
        return 1
    sk = open(PRIV, "rb").read()
    seed = open(SEED, "rb").read()
    pk = ed.publickey(sk)

    version = args.version
    if not version:
        cfg = open(os.path.join(ROOT, "src", "config.h")).read()
        import re
        m = re.search(r'REDLINE_VERSION\s+"([^"]+)"', cfg)
        version = m.group(1) if m else "0.0.0"

    rec = provlib.pack_record(provlib.owner_id(args.identity), args.serial,
                              int(time.time()), git_commit(), version)
    sig = ed.sign(rec, sk, pk)
    assert ed.verify(sig, rec, pk)
    block = provlib.embed_bytes(seed, rec, sig)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        f.write(emit_header(block, args.serial, version))
    info = provlib.unpack_record(rec)
    print(f"wrote {OUT} ({len(block)} bytes)")
    print(f"  serial {info['build_serial']}  version {info['version']}  "
          f"commit {info['git_commit'][:12]}  owner {info['owner_id']}")
    print("  commit this header; keep the seed/key secret.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
