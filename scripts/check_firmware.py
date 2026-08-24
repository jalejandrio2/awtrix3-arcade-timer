#!/usr/bin/env python3
"""Validate and optionally manifest a TC001 application image."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

MAX_APP_BYTES = 0x140000
EXPECTED_MAGIC = 0xE9


def validate(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    if not data or data[0] != EXPECTED_MAGIC:
        raise SystemExit(f"{path} is not an ESP32 application image")
    if len(data) > MAX_APP_BYTES:
        raise SystemExit(
            f"{path} is {len(data)} bytes; maximum safe application size is {MAX_APP_BYTES}"
        )
    return {
        "schema": 1,
        "version": "0.98-arcade.10",
        "base_awtrix_commit": "b8548eb4fdc8dac3fe40e9582177ec5c738530ba",
        "flash_offset": "0x10000",
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()
    manifest = validate(args.image)
    print(json.dumps(manifest, indent=2, sort_keys=True))
    if args.manifest:
        args.manifest.write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )


if __name__ == "__main__":
    main()
