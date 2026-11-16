# -*- coding: utf-8 -*-
"""Provision the upload-gate token.

Writes resources/upload_token.inc (gitignored). The value is baked into the
binary at configure time; the GUI "upload to cloud" menu item is only enabled
when VALIDPROXY_UPLOAD_TOKEN equals it at runtime.

Usage:
    # explicit value
    python scripts/provision_upload_token.py --token "my-token"

    # random token (printed once so you can copy it)
    python scripts/provision_upload_token.py --generate

    # from the environment (no value on the command line / in shell history)
    $env:VALIDPROXY_UPLOAD_TOKEN = "my-token"
    python scripts/provision_upload_token.py --from-env
"""
from __future__ import annotations

import argparse
import io
import os
import secrets
import string
import sys

TARGET = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                      'resources', 'upload_token.inc')


def generate_token(length: int = 24) -> str:
    alphabet = string.ascii_letters + string.digits
    return ''.join(secrets.choice(alphabet) for _ in range(length))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--token', help='token value to bake in')
    group.add_argument('--generate', action='store_true', help='generate a random token')
    group.add_argument('--from-env', action='store_true',
                       help='read VALIDPROXY_UPLOAD_TOKEN from the environment')
    args = parser.parse_args()

    if args.generate:
        token = generate_token()
    elif args.from_env:
        token = os.environ.get('VALIDPROXY_UPLOAD_TOKEN', '')
        if not token:
            print('VALIDPROXY_UPLOAD_TOKEN is not set', file=sys.stderr)
            return 1
    else:
        token = args.token

    token = token.strip()
    if not token:
        print('refusing to write an empty token', file=sys.stderr)
        return 1
    if any(ch.isspace() for ch in token):
        print('token must not contain whitespace', file=sys.stderr)
        return 1

    os.makedirs(os.path.dirname(TARGET), exist_ok=True)
    io.open(TARGET, 'w', encoding='utf-8', newline='').write(token + '\n')

    print('wrote %s' % TARGET)
    if args.generate:
        print('TOKEN: %s' % token)
        print('Set this in the environment before launching:')
        print('  $env:VALIDPROXY_UPLOAD_TOKEN = "%s"' % token)
    print('NOTE: this is a capability switch, not a secret. The value is')
    print('      recoverable from the binary. Reconfigure to embed it.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
