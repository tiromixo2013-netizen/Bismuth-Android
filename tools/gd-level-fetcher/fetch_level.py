#!/usr/bin/env python3
"""Download a public Geometry Dash level with official/proxy fallback."""
import base64
import gzip
import json
import sys
import zlib
from pathlib import Path

import requests

LEVEL_ID = "118509879"
SECRET = "Wmfd2893gb7"
ENDPOINTS = [
    ("Boomlings official", "https://www.boomlings.com/database/downloadGJLevel22.php"),
    ("GDProxy", "https://gdproxy.net/database/downloadGJLevel22.php"),
    ("Boomlings HTTP", "http://www.boomlings.com/database/downloadGJLevel22.php"),
]

def unpack(encoded: str) -> bytes:
    raw = base64.b64decode(encoded)
    for fn in (gzip.decompress, lambda b: zlib.decompress(b, 15 + 32), zlib.decompress):
        try:
            return fn(raw)
        except Exception:
            pass
    return raw

def extract_level(body: str):
    parts = body.split("|")
    for i in range(0, len(parts) - 1, 2):
        if parts[i] == "4":
            return parts[i + 1]
    return None

def main():
    headers = {
        "User-Agent": "",
        "Content-Type": "application/x-www-form-urlencoded",
    }
    response = None
    encoded = None
    errors = []
    print(f"Downloading public level {LEVEL_ID} with server fallback...")
    for server_name, endpoint in ENDPOINTS:
        for binary_version in ("35", "51"):
            data = {
                "gameVersion": "22",
                "binaryVersion": binary_version,
                "gdw": "0",
                "levelID": LEVEL_ID,
                "secret": SECRET,
            }
            try:
                candidate = requests.post(endpoint, data=data, headers=headers, timeout=45)
                print(f"{server_name} binaryVersion={binary_version}: HTTP {candidate.status_code}")
                candidate.raise_for_status()
                body = candidate.text.strip()
                if not body or body == "-1":
                    errors.append(f"{server_name}/{binary_version}: empty or -1 response")
                    continue
                found = extract_level(body)
                if found:
                    response, encoded = candidate, found
                    print(f"SUCCESS response from {server_name}")
                    break
                errors.append(f"{server_name}/{binary_version}: field 4 missing")
            except Exception as exc:
                errors.append(f"{server_name}/{binary_version}: {type(exc).__name__}: {exc}")
        if encoded:
            break

    if not encoded:
        raise RuntimeError("All download servers failed: " + " | ".join(errors))

    raw = unpack(encoded)
    level = raw.decode("utf-8")
    if not level:
        raise RuntimeError("Decoded level string is empty.")

    out = Path("level-output")
    out.mkdir(parents=True, exist_ok=True)
    level_file = out / f"{LEVEL_ID}.level.txt"
    level_file.write_text(level, encoding="utf-8")
    metadata = {
        "level_id": LEVEL_ID,
        "http_status": response.status_code,
        "decoded_bytes": len(raw),
        "level_string_characters": len(level),
        "semicolon_segments_estimate": sum(bool(x.strip()) for x in level.split(";")),
        "note": "Segment estimate only; use a format-aware parser for exact object statistics.",
    }
    (out / f"{LEVEL_ID}.metadata.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    print(json.dumps(metadata, indent=2))
    print(f"SUCCESS: wrote {level_file}")

if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"DOWNLOAD FAILED: {type(exc).__name__}: {exc}", file=sys.stderr)
        raise
