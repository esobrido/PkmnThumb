#!/usr/bin/env python3
"""Read a Mote .mote module's header without a device or the SDK.

A .mote file starts with 13 little-endian u32:

    [0] magic 'MOTE' (0x45544F4D)   [1] ABI version
    ...                             [12] virtual address of the version string

The version string is a plain C string in the image (MOTE_GAME_VERSION in
game.c), so an installed build can be identified by reading flash instead of
running the game.

    python3 tools/moteinfo.py game/build/PolishedCrystal.mote
    python3 tools/moteinfo.py dist/PolishedCrystal-0.1.0.mote --json
    python3 tools/moteinfo.py x.mote --manifest dist/manifest.json

Exit codes: 0 ok, 1 the file is not a usable .mote (or a check asked for failed),
2 bad usage. Checks are only performed when asked (--require-version,
--expect-abi); the ABI is compared against the SDK's MOTE_ABI_VERSION when the
vendor checkout is present, because a module built against a newer ABI than the
engine cannot load.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import struct
import sys

MAGIC = 0x45544F4D  # 'MOTE'
VADDR = 0x10800000  # the fixed window the SDK links game code at
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SDK_HEADER = os.path.join(ROOT, "vendor", "mote", "sdk", "mote_api.h")


def sdk_abi():
    """MOTE_ABI_VERSION of this checkout, or None when there is no vendor/."""
    try:
        for line in open(SDK_HEADER):
            if line.startswith("#define MOTE_ABI_VERSION"):
                return int(re.split(r"\s+", line)[2].rstrip("uU"))
    except OSError:
        pass
    return None


def info(path):
    data = open(path, "rb").read()
    if len(data) < 52:
        raise SystemExit("moteinfo: %s is too short to be a .mote" % path)
    hdr = struct.unpack_from("<13I", data, 0)
    if hdr[0] != MAGIC:
        raise SystemExit("moteinfo: %s is not a .mote (bad magic)" % path)
    abi, version_vaddr = hdr[1], hdr[12]
    version = "0"
    if abi >= 46 and version_vaddr:
        off = version_vaddr - VADDR
        if 0 <= off < len(data):
            end = data.find(b"\0", off)
            if end > off:
                version = data[off:end].decode("ascii", "replace")
    return {
        "file": os.path.basename(path),
        "name": os.path.splitext(os.path.basename(path))[0],
        "version": version,
        "abi": abi,
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def git_commit():
    try:
        import subprocess
        return subprocess.run(
            ["git", "-C", ROOT, "rev-parse", "--short", "HEAD"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
    except Exception:
        return None


def git_dirty():
    try:
        import subprocess
        out = subprocess.run(
            ["git", "-C", ROOT, "status", "--porcelain"],
            capture_output=True, text=True, check=True,
        ).stdout
        return bool(out.strip())
    except Exception:
        return None


def main():
    ap = argparse.ArgumentParser(description="inspect a Mote .mote module")
    ap.add_argument("file")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    ap.add_argument("--manifest", metavar="PATH",
                    help="write a release manifest (module info + build provenance)")
    ap.add_argument("--require-version", action="store_true",
                    help="fail if the module carries no MOTE_GAME_VERSION")
    ap.add_argument("--expect-abi", type=int,
                    help="fail if the module's ABI differs (default: the SDK's)")
    args = ap.parse_args()

    try:
        m = info(args.file)
    except OSError as exc:
        sys.exit("moteinfo: %s" % exc)

    want = args.expect_abi if args.expect_abi is not None else sdk_abi()
    problems = []
    if args.require_version and m["version"] == "0":
        problems.append("no version embedded (MOTE_GAME_VERSION missing in game.c)")
    if want is not None and m["abi"] != want:
        problems.append("ABI %d, the SDK here is %d" % (m["abi"], want))

    m["sdk_abi"] = want
    m["built"] = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    m["git_commit"] = git_commit()
    if m["git_commit"]:
        m["git_dirty"] = git_dirty()

    if args.json:
        print(json.dumps(m, indent=2))
    else:
        abi = "abi %d" % m["abi"]
        if want is not None:
            abi += " (sdk %d)" % want
        print("%s  version %s  %s  %d bytes  sha256 %s"
              % (m["name"], m["version"], abi, m["size"], m["sha256"][:16]))

    if args.manifest:
        os.makedirs(os.path.dirname(os.path.abspath(args.manifest)), exist_ok=True)
        with open(args.manifest, "w") as f:
            json.dump(m, f, indent=2)
            f.write("\n")

    for p in problems:
        print("moteinfo: %s: %s" % (args.file, p), file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
