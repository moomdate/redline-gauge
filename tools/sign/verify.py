#!/usr/bin/env python3
"""Prove whether a firmware image carries a REDLINE provenance signature.

  # dump a suspect device's flash first, e.g.:
  #   esptool.py --port /dev/cu.usbserial-XX read_flash 0x0 0x400000 suspect.bin
  python3 tools/sign/verify.py suspect.bin [--pubkey tools/sign/redline_pub.key] [--seed PATH]

Finds every embedded block (using the secret seed to locate + un-whiten it), then checks each
signature against the public key. A PASS means the image was built from source signed with the
matching private key - which only the project owner holds - and prints which build it was
(serial, version, commit, date). Standard-library only, so a third party can re-run it.

The seed only locates/decodes; it cannot forge a signature, so showing it to an expert for an
independent check does not weaken the proof.
"""
import argparse
import hashlib
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ed25519_pure as ed
import provlib

DEF_PUB = os.path.join(HERE, "redline_pub.key")
DEF_SEED = os.path.expanduser("~/Documents/works/secret/redline_prov.seed")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image", help="firmware image (flash dump or built .bin)")
    ap.add_argument("--pubkey", default=DEF_PUB)
    ap.add_argument("--seed", default=DEF_SEED)
    args = ap.parse_args()

    for p in (args.image, args.pubkey, args.seed):
        if not os.path.exists(p):
            print(f"not found: {p}", file=sys.stderr)
            return 2
    image = open(args.image, "rb").read()
    pk = open(args.pubkey, "rb").read()
    seed = open(args.seed, "rb").read()

    sha = hashlib.sha256(image).hexdigest()
    print(f"image   : {args.image}")
    print(f"size    : {len(image)} bytes")
    print(f"sha256  : {sha}")
    print(f"examined: {time.strftime('%Y-%m-%d %H:%M:%S %z')}")
    print(f"pubkey  : {pk.hex()}")
    print("-" * 60)

    blocks = provlib.extract_from_image(seed, image)
    if not blocks:
        print("RESULT: NO PROVENANCE MARK FOUND")
        print("(the image carries no block matching this seed - different seed, stripped, or unrelated)")
        return 1

    any_valid = False
    for off, rec, sig in blocks:
        ok = ed.verify(sig, rec, pk)
        info = provlib.unpack_record(rec)
        when = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(info["build_epoch"]))
        print(f"@0x{off:06x}  {'VALID' if ok else 'INVALID SIGNATURE'}")
        print(f"           owner_id    {info['owner_id']}")
        print(f"           build serial {info['build_serial']}")
        print(f"           version      {info['version']}")
        print(f"           git commit   {info['git_commit']}")
        print(f"           built (UTC)  {when}")
        any_valid = any_valid or ok

    print("-" * 60)
    if any_valid:
        print("RESULT: AUTHENTIC - signed by the REDLINE owner's private key.")
        return 0
    print("RESULT: MARK PRESENT BUT SIGNATURE INVALID (tampered, or signed by another key).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
