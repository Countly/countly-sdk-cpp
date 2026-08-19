# Security Policy

Security is very important to us. If you discover any issue regarding security, please disclose the information responsibly by sending an email to security@count.ly and not by creating a GitHub issue.

## Software Bill of Materials (SBOM)

Each GitHub release includes a CycloneDX 1.6 SBOM
(`countly-sdk-cpp-<version>.cdx.json`) with a signed attestation binding it to
the release source archive. You can also generate one for any checkout with
`python3 scripts/generate_sbom.py`, or export an SPDX SBOM of the source tree
via GitHub (Insights → Dependency graph → Export SBOM).

Because this SDK is distributed as source and its dependency set varies with
CMake options, the SBOM is a **superset**: every component the SDK can pull in
is listed, and each component's `scope` and `countly:*` properties state which
CMake option includes it.

- `required` components (e.g. nlohmann/json) are always part of the SDK.
- `optional` components are gated by a CMake option named in the
  `countly:cmake-option` property (e.g. SQLite via `COUNTLY_USE_SQLITE`).
- `excluded` components (e.g. doctest) are used only for testing and are never
  part of the shipped library.
- System-resolved libraries (OpenSSL, curl, system SQLite) carry **no version**
  by design: their versions are determined by your build environment, so their
  vulnerability exposure belongs to your build's own SBOM.

