#!/usr/bin/env python3
"""Download a public Geometry Dash level and save its decoded level string."""
import base64
import gzip
import json
import sys
import zlib
from pathlib import Path

import requests

LEVEL_ID = "118509879"
ENDPOINT = "https://www.boomlings.com/database/downloadGJLevel22.php"
SECRET = "Wmfd2893gb7"

def unpack(encoded: str) -> bytes:
    raw = base64.b64decode(encoded)
    for fn in (gzip.decompress, lambda b: zlib.decompress(b, 15 + 32), zlib.decompress):
        try:
            return fn(raw)
        except Exception:
            pass
    return raw

def main():
    data = {
        "gameVersion": "22",
        "binaryVersion": "51",
        "gdw": "0",
        "levelID": LEVEL_ID,
        "secret": SECRET,
    }
    headers = {
        "User-Agent": "",
        "Content-Type": "application/x-www-form-urlencoded",
    }
    print(f"Downloading public level {LEVEL_ID} from the Geometry Dash server...")
    response = requests.post(ENDPOINT, data=data, headers=headers, timeout=60)
    print("HTTP:", response.status_code)
    response.raise_for_status()
    body = response.text.strip()
    if not body or body == "-1":
        raise RuntimeError("Server returned -1 / no level data.")

    parts = body.split("|")
    encoded = None
    for i in range(0, len(parts) - 1, 2):
        if parts[i] == "4":
            encoded = parts[i + 1]
            break
    if encoded is None:
        raise RuntimeError("Could not find level string field 4 in response.")

    raw = unpack(encoded)
    level = raw.decode("utf-8")
    if not level:
        raise RuntimeError("Decoded level string is empty.")

    out = Path("level-output")
    out.mkdir(parents=True, exist_ok=True)
    level_file = out / f"{LEVEL_ID}.level.txt"
    level_file.write_text(level, encoding="utf-8")

    # This is a rough segment count, not an authoritative parsed object count.
    metadata = {
        "level_id": LEVEL_ID,
        "http_status": response.status_code,
        "decoded_bytes": len(raw),
        "level_string_characters": len(level),
        "semicolon_segments_estimate": sum(bool(x.strip()) for x in level.split(";")),
        "note": "Segment estimate only; use a format-aware parser for exact object statistics.",
    }
    (out / f"{LEVEL_ID}.metadata.json").write_text(
        json.dumps(metadata, indent=2), encoding="utf-8"
    )
    print(json.dumps(metadata, indent=2))
    print(f"SUCCESS: wrote {level_file}")

if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"DOWNLOAD FAILED: {type(exc).__name__}: {exc}", file=sys.stderr)
        raise
