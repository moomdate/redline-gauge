# PlatformIO pre-build hook: if the signing key is present, regenerate the signed provenance
# header for this build. If not (CI, or someone building the distributed source without the
# key), leave the committed header in place so the build still succeeds.
import os
import subprocess
import sys

Import("env")  # noqa: F821  (PlatformIO injects this)

ROOT = env["PROJECT_DIR"]  # noqa: F821
KEY = os.path.expanduser("~/Documents/works/secret/redline_sign_ed25519.key")
SERIAL = os.environ.get("PROV_SERIAL", "1")

# Regenerate only on an intentional release build (PROV_SERIAL set). Plain `pio run` keeps the
# committed header, so the tree does not go dirty on every build.
if os.path.exists(KEY) and "PROV_SERIAL" in os.environ:
    print(f"[prov] release build: regenerating mark (serial {SERIAL})")
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "sign", "make_record.py"),
                        "--serial", SERIAL], cwd=ROOT)
    if r.returncode != 0:
        print("[prov] make_record failed; keeping the committed header")
elif not os.path.exists(KEY):
    print("[prov] no signing key on this machine; using the committed mark as-is")
else:
    print("[prov] using the committed mark (set PROV_SERIAL to regenerate for a release)")
