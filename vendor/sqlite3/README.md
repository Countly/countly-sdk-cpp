# SQLite amalgamation (vendored)

This directory contains the official SQLite amalgamation (`sqlite3.c` /
`sqlite3.h`) from https://sqlite.org, vendored verbatim.

- **Version:** 3.53.4
- **Source:** https://sqlite.org/2026/sqlite-amalgamation-3530400.zip
- **SHA3-256 (zip):** `628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e`
  (matches the value published on https://sqlite.org/download.html)
- **License:** public domain (https://sqlite.org/copyright.html)

When `COUNTLY_USE_SQLITE=ON` (and `COUNTLY_USE_SYSTEM_SQLITE=OFF`, the
default), these files are compiled directly into the countly library, so the
header and library versions always match. Set `COUNTLY_USE_SYSTEM_SQLITE=ON`
to link the platform's SQLite instead (keeps binaries smaller when a system
SQLite is available).

## Updating

Run `python3 scripts/update_sqlite.py <version>` (e.g. `3.54.0`). The script
downloads the release from sqlite.org, verifies the SHA3-256 it prints against
the one published on the download page, and replaces the files here. Update
the version, URL, and hash above in the same commit.
