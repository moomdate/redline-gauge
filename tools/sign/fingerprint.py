#!/usr/bin/env python3
"""Build a signed fingerprint manifest of REDLINE's distinctive core data.

  python3 tools/sign/fingerprint.py            # (re)build tools/sign/core_manifest.json

For each listed blob (background art, logo, fonts) it records a byte-identical window plus the
full SHA-256. These blobs end up verbatim in the firmware image; an infringer who reskins the UI
still ships them, and removing them means replacing your entire asset set - so they are a mark
that lives in the suspect's own copy of your work, with nothing to strip out.

The manifest is signed with the owner's Ed25519 key (if present) so it cannot be faked; commit it
so git history timestamps when you declared these fingerprints. verify.py --scan uses it.
"""
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import ed25519_pure as ed
import provlib

PRIV = os.path.expanduser("~/Documents/works/secret/redline_sign_ed25519.key")
OUT = os.path.join(HERE, "core_manifest.json")
IDENTITY = "moomdate"

# distinctive, large, owner-made blobs that a reskin keeps
BLOBS = [
    "src/assets/bg_ice.h",
    "src/assets/bg_amber.h",
    "src/assets/bg_lime.h",
    "src/assets/logo_birdlab.h",
    "src/fonts/font_rpm.h",
    "src/fonts/font_speed.h",
]


def main():
    entries = []
    for rel in BLOBS:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            print(f"  skip (missing): {rel}")
            continue
        e = provlib.fingerprint_entry(path)
        e["file"] = rel
        entries.append(e)
        print(f"  {e['symbol']:<18} {e['bytes']:>7} B  sha256 {e['sha256'][:12]}…")
    entries.sort(key=lambda e: e["file"])

    payload = {"owner_id": provlib.owner_id(IDENTITY).hex(), "entries": entries}
    msg = json.dumps(payload, sort_keys=True, separators=(",", ":")).encode()

    sig = ""
    if os.path.exists(PRIV):
        sk = open(PRIV, "rb").read()
        sig = ed.sign(msg, sk).hex()
        print("  signed with the owner key")
    else:
        print("  no owner key present: manifest is UNSIGNED (run keygen.py to sign it)")

    manifest = {"format": "redline-core-fingerprint/1", "generated": int(time.time()),
                "owner_id": payload["owner_id"], "entries": entries, "signature": sig}
    with open(OUT, "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"wrote {os.path.relpath(OUT, ROOT)} ({len(entries)} blobs)")
    print("commit it so its git timestamp records when you declared these fingerprints.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
