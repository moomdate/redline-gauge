# Build provenance (anti-resale proof)

Proves that a firmware image descends from a REDLINE build signed by the owner, even after the
UI is replaced. For enforcing the PolyForm Noncommercial licence against someone reselling the work.

Security rests on an **Ed25519 signature**: the private key never ships, so no one but the owner
can produce a mark that `verify.py` accepts. This holds even though the scheme is public
(Kerckhoffs) - knowing how it works does not let anyone forge it.

## One-time setup
```
python3 tools/sign/keygen.py            # writes the private key + seed to ~/Documents/works/secret/
                                        # and the public key to tools/sign/redline_pub.key (commit it)
```
Back up `~/Documents/works/secret/redline_sign_ed25519.key` offline. Lose it = cannot sign as this
identity again.

## Per release / per recipient
```
PROV_SERIAL=<n> pio run -e esp32dev     # the pre-build hook regenerates src/core/prov_data.h
# or by hand:
python3 tools/sign/make_record.py --serial <n>
```
Give each person / batch a different `--serial`; a leaked, resold image then tells you which line
it came from. Commit the regenerated `src/core/prov_data.h`.

## Two layers of proof
- **Core fingerprint** (`tools/sign/fingerprint.py` -> `core_manifest.json`, signed + committed):
  byte-identical windows of your own art / logo / fonts. A reskinned clone still ships them, and
  `verify.py` finds them in the suspect image. Nothing to strip - it is their copy of your work.
- **Embedded mark** (disguised as `src/core/shade_lut.h`, a "shading LUT"): a signed record that
  also carries the build serial, so a leaked copy can be traced to who received it. Removing it
  makes the dash show **UNOFFICIAL** (bound into `splash_ui::authentic()`), and the fingerprint
  still catches the clone.

## Proving infringement
```
esptool.py --port <PORT> read_flash 0x0 0x400000 suspect.bin   # dump the suspect device
python3 tools/sign/verify.py suspect.bin                       # PASS = built from your signed source
```
`verify.py` is standard-library only, so an expert or a court can re-run it. For evidence: record
how the image was obtained and when (chain of custody), keep the printed sha256 + timestamp.

## Limits (be honest)
- **Not unremovable.** Someone who rebuilds from scratch without the core carries no mark. Someone
  who has the source can find and delete `src/core/prov_data.h` / `provenance.*`. The signature is
  unforgeable; the *presence* of the mark is not guaranteed against a determined source-level editor.
- Because this repo is distributed as source, consider for the shipped copy: (a) keep `tools/sign/`
  out of the distributed tree, (b) disguise the embedded bytes as an innocuous data table referenced
  by core math so removal changes behaviour, or (c) distribute binaries only. See the project owner.
- Not legal advice: register copyright and consult an IP lawyer before acting.
