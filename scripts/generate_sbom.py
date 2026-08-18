#!/usr/bin/env python3
"""Generate a CycloneDX 1.6 superset SBOM for countly-sdk-cpp.

The SDK is distributed as source and its dependency set varies with CMake
options, so this emits a single "superset" SBOM: every component the SDK can
pull in is listed, with `scope` and `countly:*` properties expressing which
CMake option gates it. System libraries resolved on the consumer's machine
(OpenSSL, libcurl) are listed without a version by design.

Versions of vendored submodules are read live from git, the SDK version from
constants.hpp, so the output can never drift from the checked-out tree.

Usage: python3 scripts/generate_sbom.py [-o OUTPUT.json]
Requires only the Python standard library and git.
"""

import argparse
import datetime
import json
import re
import subprocess
import sys
import uuid
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def sdk_version() -> str:
    contents = (REPO_ROOT / "include/countly/constants.hpp").read_text()
    match = re.search(r'#define COUNTLY_SDK_VERSION "([^"]+)"', contents)
    if not match:
        sys.exit("error: COUNTLY_SDK_VERSION not found in constants.hpp")
    return match.group(1)


def git(path: Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(path), *args], text=True).strip()


def submodule_pin(name: str) -> dict:
    """Return commit hash and nearest-tag description for a vendored submodule."""
    path = REPO_ROOT / "vendor" / name
    commit = git(path, "rev-parse", "HEAD")
    try:
        described = git(path, "describe", "--tags")
    except subprocess.CalledProcessError:
        described = commit[:12]
    return {"commit": commit, "described": described}


def sqlite_version() -> str:
    contents = (REPO_ROOT / "vendor/sqlite3/sqlite3.h").read_text()
    match = re.search(r'#define SQLITE_VERSION\s+"([^"]+)"', contents)
    if not match:
        sys.exit("error: SQLITE_VERSION not found in vendor/sqlite3/sqlite3.h")
    return match.group(1)


def prop(name: str, value: str) -> dict:
    return {"name": name, "value": value}


def build_sbom() -> dict:
    version = sdk_version()
    json_pin = submodule_pin("json")
    sqlite_ver = sqlite_version()
    doctest_pin = submodule_pin("doctest")

    components = [
        {
            "bom-ref": "nlohmann-json",
            "type": "library",
            "name": "json",
            "group": "nlohmann",
            "version": json_pin["described"],
            "purl": f"pkg:github/nlohmann/json@{json_pin['commit']}",
            "scope": "required",
            "licenses": [{"license": {"id": "MIT"}}],
            "properties": [
                prop("countly:origin", "vendored git submodule (vendor/json)"),
                prop("countly:condition", "always included; header-only, part of the SDK's public API surface"),
                prop("countly:pinned-commit", json_pin["commit"]),
            ],
        },
        {
            "bom-ref": "sqlite3",
            "type": "library",
            "name": "sqlite",
            "group": "sqlite",
            "version": sqlite_ver,
            "purl": f"pkg:generic/sqlite@{sqlite_ver}?download_url=https://sqlite.org",
            "cpe": f"cpe:2.3:a:sqlite:sqlite:{sqlite_ver}:*:*:*:*:*:*:*",
            "scope": "optional",
            "licenses": [{"license": {"id": "blessing"}}],
            "properties": [
                prop("countly:origin", "official sqlite.org amalgamation vendored in vendor/sqlite3, compiled into the library"),
                prop("countly:cmake-option", "COUNTLY_USE_SQLITE=ON (with COUNTLY_USE_SYSTEM_SQLITE=OFF, the default)"),
                prop("countly:condition", "compiled in only when COUNTLY_USE_SQLITE=ON and COUNTLY_USE_SYSTEM_SQLITE=OFF"),
                prop("countly:compile-flags", "SQLITE_OMIT_LOAD_EXTENSION SQLITE_OMIT_DEPRECATED SQLITE_DEFAULT_MEMSTATUS=0; FTS/RTree/Geopoly/RBU/ICU not enabled"),
            ],
        },
        {
            "bom-ref": "sqlite3-system",
            "type": "library",
            "name": "sqlite (system)",
            "purl": "pkg:generic/sqlite",
            "scope": "optional",
            "properties": [
                prop("countly:origin", "system library, resolved at consumer build time (find_package(SQLite3))"),
                prop("countly:cmake-option", "COUNTLY_USE_SQLITE=ON with COUNTLY_USE_SYSTEM_SQLITE=ON"),
                prop("countly:condition", "linked instead of the vendored amalgamation when COUNTLY_USE_SYSTEM_SQLITE=ON; version is determined by the consumer's environment"),
            ],
        },
        {
            "bom-ref": "openssl",
            "type": "library",
            "name": "openssl",
            "purl": "pkg:generic/openssl",
            "scope": "optional",
            "properties": [
                prop("countly:origin", "system library, resolved at consumer build time (find_package(OpenSSL))"),
                prop("countly:cmake-option", "COUNTLY_USE_CUSTOM_SHA256=OFF (default)"),
                prop("countly:condition", "linked unless the consumer supplies a custom SHA-256 implementation; version is determined by the consumer's environment"),
            ],
        },
        {
            "bom-ref": "libcurl",
            "type": "library",
            "name": "curl",
            "purl": "pkg:generic/curl",
            "scope": "optional",
            "properties": [
                prop("countly:origin", "system library, resolved at consumer build time (find_package(CURL))"),
                prop("countly:cmake-option", "COUNTLY_USE_CUSTOM_HTTP=OFF (default)"),
                prop("countly:condition", "linked on non-Windows platforms unless the consumer supplies a custom HTTP function; version is determined by the consumer's environment"),
            ],
        },
        {
            "bom-ref": "winhttp",
            "type": "library",
            "name": "WinHTTP",
            "scope": "optional",
            "properties": [
                prop("countly:origin", "Windows operating-system component"),
                prop("countly:cmake-option", "COUNTLY_USE_CUSTOM_HTTP=OFF (default)"),
                prop("countly:condition", "used on Windows unless the consumer supplies a custom HTTP function"),
            ],
        },
        {
            "bom-ref": "doctest",
            "type": "library",
            "name": "doctest",
            "group": "doctest",
            "version": doctest_pin["described"],
            "purl": f"pkg:github/doctest/doctest@{doctest_pin['commit']}",
            "scope": "excluded",
            "licenses": [{"license": {"id": "MIT"}}],
            "properties": [
                prop("countly:origin", "vendored git submodule (vendor/doctest)"),
                prop("countly:cmake-option", "COUNTLY_BUILD_TESTS=ON"),
                prop("countly:condition", "test framework only; never part of the shipped library"),
                prop("countly:pinned-commit", doctest_pin["commit"]),
            ],
        },
    ]

    return {
        "bomFormat": "CycloneDX",
        "specVersion": "1.6",
        "serialNumber": f"urn:uuid:{uuid.uuid4()}",
        "version": 1,
        "metadata": {
            "timestamp": datetime.datetime.now(datetime.timezone.utc)
            .isoformat(timespec="seconds")
            .replace("+00:00", "Z"),
            "supplier": {"name": "Countly", "url": ["https://count.ly"]},
            "component": {
                "bom-ref": "countly-sdk-cpp",
                "type": "library",
                "name": "countly-sdk-cpp",
                "group": "countly",
                "version": version,
                "purl": f"pkg:github/countly/countly-sdk-cpp@{version}",
                "licenses": [{"license": {"id": "MIT"}}],
                "externalReferences": [
                    {"type": "vcs", "url": "https://github.com/Countly/countly-sdk-cpp"},
                ],
            },
            "properties": [
                prop(
                    "countly:sbom-type",
                    "superset source SBOM: optional-scope components are gated by CMake "
                    "options; system libraries carry no version because the consumer's "
                    "build resolves them",
                ),
            ],
        },
        "components": components,
        "dependencies": [
            {
                "ref": "countly-sdk-cpp",
                "dependsOn": [c["bom-ref"] for c in components if c["scope"] != "excluded"],
            }
        ],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("-o", "--output", type=Path, default=None,
                        help="output path (default: countly-sdk-cpp-<version>.cdx.json in the repo root)")
    args = parser.parse_args()

    sbom = build_sbom()
    out = args.output or REPO_ROOT / f"countly-sdk-cpp-{sbom['metadata']['component']['version']}.cdx.json"
    out.write_text(json.dumps(sbom, indent=2) + "\n")
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
