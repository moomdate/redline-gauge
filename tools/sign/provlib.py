"""Shared format for the REDLINE provenance record.

The record proves which build a firmware image came from. It is signed with the project's
Ed25519 private key (never shipped) and verified with the public key (shipped, public).
Before it goes into the firmware it is XOR-whitened with a keystream derived from a secret
seed, so it reads as noise in a flash dump and can only be located / un-whitened by someone
who holds the seed. The seed hides and finds the record; it does NOT protect against forgery
- that is the signature's job - so revealing the seed to an expert for verification is safe.

Layout (little-endian):
  record  64 bytes: magic 'RLP1' | owner_id[16] | build_serial u32 | build_epoch u64 |
                    git_commit[20] | version[8] ascii | reserved[4]
  signature 64 bytes = Ed25519(private_key, record)
  embedded in firmware = locator[8] || whiten(record || signature)   # 8 + 128 = 136 bytes
"""
import hashlib
import struct

MAGIC = b"RLP1"
RECORD_LEN = 64
SIG_LEN = 64
LOCATOR_LEN = 8
EMBED_LEN = LOCATOR_LEN + RECORD_LEN + SIG_LEN   # 136


def owner_id(identity: str) -> bytes:
    """Stable 16-byte id for an owner string (e.g. 'moomdate|<email>'); not reversible to the email."""
    return hashlib.sha256(b"REDLINE-OWNER|" + identity.encode()) .digest()[:16]


def pack_record(oid: bytes, serial: int, epoch: int, git_commit: bytes, version: str) -> bytes:
    assert len(oid) == 16 and len(git_commit) == 20
    ver = version.encode()[:8].ljust(8, b"\0")
    rec = MAGIC + oid + struct.pack("<IQ", serial & 0xFFFFFFFF, epoch) + git_commit + ver + b"\0\0\0\0"
    assert len(rec) == RECORD_LEN, len(rec)
    return rec


def unpack_record(rec: bytes) -> dict:
    if len(rec) != RECORD_LEN or rec[:4] != MAGIC:
        raise ValueError("not a REDLINE record")
    oid = rec[4:20]
    serial, epoch = struct.unpack("<IQ", rec[20:32])
    git_commit = rec[32:52]
    version = rec[52:60].rstrip(b"\0").decode("ascii", "replace")
    return {"owner_id": oid.hex(), "build_serial": serial, "build_epoch": epoch,
            "git_commit": git_commit.hex(), "version": version}


def _keystream(seed: bytes, tag: bytes, n: int) -> bytes:
    out = bytearray()
    ctr = 0
    while len(out) < n:
        out += hashlib.sha256(seed + tag + struct.pack("<I", ctr)).digest()
        ctr += 1
    return bytes(out[:n])


def locator(seed: bytes) -> bytes:
    return hashlib.sha256(seed + b"REDLINE-LOC").digest()[:LOCATOR_LEN]


def whiten(seed: bytes, data: bytes) -> bytes:
    ks = _keystream(seed, b"REDLINE-ENC", len(data))
    return bytes(a ^ b for a, b in zip(data, ks))


# whiten is its own inverse
unwhiten = whiten


def embed_bytes(seed: bytes, record: bytes, signature: bytes) -> bytes:
    """The 136-byte block to place in the firmware."""
    return locator(seed) + whiten(seed, record + signature)


def extract_from_image(seed: bytes, image: bytes):
    """Find every embedded block in a firmware image; returns list of (offset, record, signature)."""
    loc = locator(seed)
    found = []
    start = 0
    while True:
        i = image.find(loc, start)
        if i < 0:
            break
        start = i + 1
        blk = image[i + LOCATOR_LEN:i + EMBED_LEN]
        if len(blk) == RECORD_LEN + SIG_LEN:
            clear = unwhiten(seed, blk)
            rec, sig = clear[:RECORD_LEN], clear[RECORD_LEN:]
            if rec[:4] == MAGIC:
                found.append((i, rec, sig))
    return found
