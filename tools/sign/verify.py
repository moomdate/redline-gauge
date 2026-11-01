#!/usr/bin/env python3
"""Prove whether a firmware image came from REDLINE's core.

  # dump a suspect device's flash first, e.g.:
  #   esptool.py --port /dev/cu.usbserial-XX read_flash 0x0 0x400000 suspect.bin
  python3 tools/sign/verify.py suspect.bin

Two independent proofs, reported together:

 1. CORE FINGERPRINT (primary, nothing to strip): searches the image for byte-identical windows
    of the owner's own data blobs (background art, logo, fonts) listed in a signed manifest.
    A reskinned clone still ships these, so finding them proves it was built from your core.
    Needs only the public key + tools/sign/core_manifest.json (both committed) - no secret.

 2. EMBEDDED MARK (secondary, carries build serial): a signed record hidden in the image.
    Needs the secret whitening seed to locate; gives the exact build serial / version / date
    for tracing which handed-out copy leaked. Skipped if the seed isn't available.

Standard-library only, so a third party or a court can re-run it. For evidence, note how the
image was obtained and when (chain of custody); keep the printed sha256 + timestamp.
"""
import argparse
import hashlib
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ed25519_pure as ed
import provlib

DEF_PUB = os.path.join(HERE, "redline_pub.key")
DEF_SEED = os.path.expanduser("~/Documents/works/secret/redline_prov.seed")
DEF_MANIFEST = os.path.join(HERE, "core_manifest.json")


def scan_fingerprint(image, pk, manifest_path):
    if not os.path.exists(manifest_path):
        print("CORE FINGERPRINT: no manifest (run tools/sign/fingerprint.py)")
        return False
    man = json.load(open(manifest_path))
    entries = man["entries"]
    payload = {"owner_id": man["owner_id"], "entries": entries}
    msg = json.dumps(payload, sort_keys=True, separators=(",", ":")).encode()
    sig_ok = bool(man.get("signature")) and ed.verify(bytes.fromhex(man["signature"]), msg, pk)
    print(f"CORE FINGERPRINT  (manifest signature: {'VALID' if sig_ok else 'UNSIGNED/INVALID'})")
    present = 0
    for e in entries:
        win = bytes.fromhex(e["window_hex"])
        at = image.find(win)
        ok = at >= 0
        present += ok
        print(f"  {'FOUND ' if ok else 'absent'} {e['symbol']:<18} "
              + (f"@0x{at:06x}" if ok else "") + f"   ({e['bytes']} B blob)")
    print(f"  -> {present}/{len(entries)} core blobs present")
    # a strong, trustworthy match: the manifest is really the owner's, and their blobs are here
    return sig_ok and present >= 2


def check_embedded(image, pk, seed_path):
    if not os.path.exists(seed_path):
        print("EMBEDDED MARK     : skipped (no whitening seed on this machine)")
        return False
    seed = open(seed_path, "rb").read()
    blocks = provlib.extract_from_image(seed, image)
    if not blocks:
        print("EMBEDDED MARK     : none found (stripped, different seed, or unrelated)")
        return False
    any_valid = False
    for off, rec, sig in blocks:
        ok = ed.verify(sig, rec, pk)
        info = provlib.unpack_record(rec)
        when = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(info["build_epoch"]))
        print(f"EMBEDDED MARK     : @0x{off:06x} {'VALID' if ok else 'INVALID'}  "
              f"serial {info['build_serial']}, v{info['version']}, "
              f"commit {info['git_commit'][:12]}, built {when} UTC")
        any_valid = any_valid or ok
    return any_valid


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image", help="firmware image (flash dump or built .bin)")
    ap.add_argument("--pubkey", default=DEF_PUB)
    ap.add_argument("--seed", default=DEF_SEED)
    ap.add_argument("--manifest", default=DEF_MANIFEST)
    args = ap.parse_args()

    for p in (args.image, args.pubkey):
        if not os.path.exists(p):
            print(f"not found: {p}", file=sys.stderr)
            return 2
    image = open(args.image, "rb").read()
    pk = open(args.pubkey, "rb").read()

    print(f"image   : {args.image}")
    print(f"size    : {len(image)} bytes")
    print(f"sha256  : {hashlib.sha256(image).hexdigest()}")
    print(f"examined: {time.strftime('%Y-%m-%d %H:%M:%S %z')}")
    print(f"pubkey  : {pk.hex()}")
    print("-" * 64)
    fp = scan_fingerprint(image, pk, args.manifest)
    print("-" * 64)
    mk = check_embedded(image, pk, args.seed)
    print("-" * 64)
    if fp or mk:
        print("RESULT: AUTHENTIC - this image was built from the REDLINE owner's core.")
        return 0
    print("RESULT: NO OWNER MARK FOUND.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
