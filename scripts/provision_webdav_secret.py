#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Provision the embedded WebDAV credential blob.

Writes resources/webdav_secret.inc, which CMake embeds into the executable via
configure_file. The file is gitignored on purpose: credentials must never enter
the repository or any log.

SECURITY: the blob is XOR+Base64 with a key that also ships in the binary, so it
is obfuscation, not confidentiality (see docs/specs/2026-11-13-Spec-WebDavCloudSync-v1.0.md 5).

Usage:
    python scripts/provision_webdav_secret.py                 # interactive
    VALIDPROXY_WEBDAV_USER=... VALIDPROXY_WEBDAV_PASS=... \
        python scripts/provision_webdav_secret.py --from-env  # non-interactive
"""
import argparse
import base64
import os
import sys

KEY = b"validproxy-webdav-v1"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "resources", "webdav_secret.inc")


def xor(data: bytes, key: bytes) -> bytes:
    return bytes(b ^ key[i % len(key)] for i, b in enumerate(data))


def encode(user: str, password: str) -> str:
    plain = (user + "\n" + password).encode("utf-8")
    return base64.b64encode(xor(plain, KEY)).decode("ascii")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--from-env", action="store_true",
                        help="read VALIDPROXY_WEBDAV_USER / VALIDPROXY_WEBDAV_PASS")
    parser.add_argument("--dry-run", action="store_true", help="print the target path only")
    args = parser.parse_args()

    if args.from_env:
        user = os.environ.get("VALIDPROXY_WEBDAV_USER", "")
        password = os.environ.get("VALIDPROXY_WEBDAV_PASS", "")
    else:
        user = input("WebDAV account (Jianguoyun login email): ").strip()
        import getpass
        password = getpass.getpass("WebDAV app password (NOT the login password): ")

    if not user or not password:
        print("ERROR: account and app password are both required", file=sys.stderr)
        return 2

    if args.dry_run:
        print("would write:", OUT)
        return 0

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    blob = encode(user, password)
    with open(OUT, "w", encoding="ascii", newline="\n") as fh:
        fh.write(blob + "\n")

    print("wrote", OUT, "(%d bytes)" % len(blob))
    print("NOTE: this is obfuscation, not encryption. Rebuild to embed it.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
