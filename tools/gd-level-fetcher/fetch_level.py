#!/usr/bin/env python3
"""Download a public Geometry Dash level with API and server fallbacks."""
import base64
import gzip
import json
import sys
import zlib
from pathlib import Path

import requests

LEVEL_ID = "118509879"
SECRET = "Wmfd2893gb7"
GDBROWSER_URL = f"https://gdbrowser.com/api/level/{LEVEL_ID}?download"
ENDPOINTS = [
    ("Boomlings official", "https://www.boomlings.com/database/downloadGJLevel22.php"),
    ("GDProxy", "https://gdproxy.net/database/downloadGJLevel22.php"),
    ("Boomlings HTTP", "http://www.boomlings.com/database/downloadGJLevel22.php"),
]

def decode_payload(value):
    """Accept GDBrowser's compressed data field or a raw level string."""
    if not isinstance(value, str) or not value:
        raise ValueError("missing/empty level data")
    try:
        raw = base64.b64decode(value, validate=True)
        for fn in (gzip.decompress, lambda b: zlib.decompress(b, 15 + 32), zlib.decompress):
            try:
                decoded = fn(raw)
                if decoded:
                    return decoded.decode("utf-8")
            except Exception:
                pass
    except Exception:
        pass
    return value

def extract_level(body: str):
    parts = body.split("|")
    for i in range(0, len(parts) - 1, 2):
        if parts[i] == "4":
            return parts[i + 1]
    return None

def save_level(level: str, source: str, http_status: int):
    if not level or len(level) < 20:
        raise ValueError(f"decoded level string is suspiciously short ({len(level) if level else 0})")
    out = Path("level-output")
    out.mkdir(parents=True, exist_ok=True)
    level_file = out / f"{LEVEL_ID}.level.txt"
    level_file.write_text(level, encoding="utf-8")
    metadata = {
        "level_id": LEVEL_ID,
        "source": source,
        "http_status": http_status,
        "level_string_characters": len(level),
        "semicolon_segments_estimate": sum(bool(x.strip()) for x in level.split(";")),
        "note": "Segment estimate only; use a format-aware parser for exact object statistics.",
    }
    (out / f"{LEVEL_ID}.metadata.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    print(json.dumps(metadata, indent=2))
    print(f"SUCCESS: wrote {level_file}")

def main():
    session = requests.Session()
    session.headers.update({"User-Agent": "Mozilla/5.0 (compatible; GDLevelResearch/1.0)"})
    errors = []
    print(f"Downloading public level {LEVEL_ID}...")
    
    # First try GDBrowser's documented download=true API.
    try:
        response = session.get(GDBROWSER_URL, timeout=45)
        print(f"GDBrowser API: HTTP {response.status_code}")
        response.raise_for_status()
        payload = response.json()
        if isinstance(payload, dict):
            # GDBrowser's download flag is presence-based (?download), not download=true.
            candidates = ("data", "levelData", "levelString", "level_string", "level")
            for key in candidates:
                value = payload.get(key)
                if isinstance(value, str) and value:
                    level = decode_payload(value)
                    save_level(level, f"GDBrowser API ?download (field={key})", response.status_code)
                    return
            errors.append(f"GDBrowser: download response has no recognized level payload (keys={list(payload)})")
        else:
            errors.append(f"GDBrowser: unexpected JSON type {type(payload).__name__}")
    except Exception as exc:
        errors.append(f"GDBrowser: {type(exc).__name__}: {exc}")

    # Fallback to the game's native download endpoint through available proxies.
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
                candidate = session.post(endpoint, data=data, timeout=45)
                print(f"{server_name} binaryVersion={binary_version}: HTTP {candidate.status_code}")
                candidate.raise_for_status()
                body = candidate.text.strip()
                found = extract_level(body)
                if found:
                    level = decode_payload(found)
                    save_level(level, f"{server_name} binaryVersion={binary_version}", candidate.status_code)
                    return
                errors.append(f"{server_name}/{binary_version}: response preview={body[:180]!r}")
            except Exception as exc:
                errors.append(f"{server_name}/{binary_version}: {type(exc).__name__}: {exc}")

    raise RuntimeError("All download methods failed: " + " | ".join(errors))

if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"DOWNLOAD FAILED: {type(exc).__name__}: {exc}", file=sys.stderr)
        raise
