#!/usr/bin/env python3
"""Update the vendored SQLite amalgamation in vendor/sqlite3/.

Usage: python3 scripts/update_sqlite.py <version> [--year YYYY]
   e.g. python3 scripts/update_sqlite.py 3.54.0

Downloads the official amalgamation zip from sqlite.org, prints its SHA3-256
for verification against https://sqlite.org/download.html, and replaces
vendor/sqlite3/sqlite3.{c,h}. Refuses to proceed unless the hash on the
download page (fetched automatically) matches the downloaded file.

Requires only the Python standard library.
"""

import argparse
import hashlib
import io
import re
import sys
import urllib.request
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
VENDOR_DIR = REPO_ROOT / "vendor" / "sqlite3"


def release_number(version: str) -> str:
    """3.54.0 -> 3540000 (sqlite.org release numbering)."""
    parts = version.split(".")
    if len(parts) == 2:
        parts.append("0")
    major, minor, patch = (int(p) for p in parts)
    return f"{major}{minor:02d}{patch:02d}00"


def fetch(url: str) -> bytes:
    with urllib.request.urlopen(url) as response:
        return response.read()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("version", help="SQLite version, e.g. 3.54.0")
    parser.add_argument("--year", default=None,
                        help="release-year path segment on sqlite.org (default: parse from download page)")
    args = parser.parse_args()

    number = release_number(args.version)
    zip_name = f"sqlite-amalgamation-{number}.zip"

    download_page = fetch("https://sqlite.org/download.html").decode("utf-8", "replace")
    year = args.year
    if year is None:
        match = re.search(rf"(\d{{4}})/{re.escape(zip_name)}", download_page)
        if not match:
            sys.exit(f"error: {zip_name} not found on sqlite.org/download.html "
                     "(wrong version, or pass --year explicitly)")
        year = match.group(1)

    published = None
    at = download_page.find(zip_name)
    if at != -1:
        match = re.search(r"SHA3-256: ([a-f0-9]{64})", download_page[at:])
        if match:
            published = match.group(1)
    if published is None:
        sys.exit("error: could not find published SHA3-256 on the download page")

    url = f"https://sqlite.org/{year}/{zip_name}"
    print(f"downloading {url}")
    payload = fetch(url)
    digest = hashlib.sha3_256(payload).hexdigest()
    print(f"downloaded SHA3-256: {digest}")
    print(f"published  SHA3-256: {published}")
    if digest != published:
        sys.exit("error: checksum mismatch — aborting, nothing was modified")

    with zipfile.ZipFile(io.BytesIO(payload)) as archive:
        prefix = f"sqlite-amalgamation-{number}"
        for name in ("sqlite3.c", "sqlite3.h"):
            (VENDOR_DIR / name).write_bytes(archive.read(f"{prefix}/{name}"))
            print(f"wrote vendor/sqlite3/{name}")

    print(f"done — update version/URL/hash in vendor/sqlite3/README.md "
          f"and commit (SQLite {args.version}, SHA3-256 {digest})")


if __name__ == "__main__":
    main()
