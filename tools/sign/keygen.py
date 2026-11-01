#!/usr/bin/env python3
"""Generate the REDLINE provenance keypair and seed. Run ONCE. Keep the output secret.

  python3 tools/sign/keygen.py

Writes, by default into ~/Documents/works/secret/ (OUTSIDE the repo):
  redline_sign_ed25519.key   32-byte Ed25519 private seed  -- NEVER commit, NEVER share
  redline_prov.seed          32-byte whitening seed         -- secret; may be shown to an
                                                               expert for verification only
And into the repo (safe to commit, needed to verify):
  tools/sign/redline_pub.key 32-byte Ed25519 public key

If the keypair already exists it refuses to overwrite (losing the private key means you can
no longer sign new builds under the same identity).
"""
import os
import sys
import secrets

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ed25519_pure as ed

SECRET_DIR = os.path.expanduser("~/Documents/works/secret")
PRIV = os.path.join(SECRET_DIR, "redline_sign_ed25519.key")
SEED = os.path.join(SECRET_DIR, "redline_prov.seed")
PUB = os.path.join(os.path.dirname(os.path.abspath(__file__)), "redline_pub.key")


def main():
    os.makedirs(SECRET_DIR, exist_ok=True)
    if os.path.exists(PRIV) or os.path.exists(SEED):
        print(f"refusing to overwrite existing secret material in {SECRET_DIR}")
        print("delete it by hand first if you really mean to start a new identity.")
        return 1
    sk = secrets.token_bytes(32)
    seed = secrets.token_bytes(32)
    pk = ed.publickey(sk)
    for path, data in ((PRIV, sk), (SEED, seed)):
        with open(path, "wb") as f:
            f.write(data)
        os.chmod(path, 0o600)
    with open(PUB, "wb") as f:
        f.write(pk)
    print("keypair created.")
    print(f"  private seed : {PRIV}   (0600, never commit)")
    print(f"  whitening seed: {SEED}   (0600, secret)")
    print(f"  public key   : {PUB}   (commit this)")
    print(f"  public key hex: {pk.hex()}")
    print("\nback these up offline. losing the private seed = cannot sign as this identity again.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
