# Package manager files

The vcpkg port and the Conan recipe for this SDK, laid out as in their upstream repositories, so a release can be
submitted by copying them over:

- `vcpkg/ports/countly-sdk-cpp` goes to `ports/countly-sdk-cpp` in [microsoft/vcpkg](https://github.com/microsoft/vcpkg).
- `conan/countly-sdk-cpp` goes to `recipes/countly-sdk-cpp` in [conan-io/conan-center-index](https://github.com/conan-io/conan-center-index).

Both build against the package manager's own nlohmann/json, OpenSSL, libcurl and (with the `sqlite` feature or the
`with_sqlite` option) SQLite, and install the CMake package used as:

```cmake
find_package(countly CONFIG REQUIRED)
target_link_libraries(main PRIVATE countly::countly)
```

## At a release

1. Set the version in `vcpkg/ports/countly-sdk-cpp/vcpkg.json`, `conan/countly-sdk-cpp/config.yml` and
   `conan/countly-sdk-cpp/all/conandata.yml`.
2. Fill in the hashes:
   - `SHA512` in `portfile.cmake`, for GitHub's archive of the tag, which vcpkg downloads (vcpkg prints the expected
     value when it is left at `0`).
   - `sha256` in `conandata.yml`, for `countly-sdk-cpp-<version>.tar.gz`, the source archive the SBOM workflow attaches
     to the release (ConanCenter prefers an official archive to a generated one). That archive must not be replaced
     after the recipe uses its hash.
3. Open a pull request in each upstream repository, following their contribution guides.

## Testing locally

```sh
vcpkg install countly-sdk-cpp --overlay-ports=packaging/vcpkg/ports
conan create packaging/conan/countly-sdk-cpp/all --version <version> --build=missing
```

Both download the tagged release, so before a tag exists, point the source in the port and in `conandata.yml` at a
local archive of the tree instead.

## In CI

`.github/workflows/packaging.yml` does that for every commit that touches the build or these files: it packs the commit
with `git archive` (which, like GitHub's release archives, leaves the submodules out), builds the port and the recipe
from that archive on Linux, Windows and macOS, and builds an app against each, without and with SQLite. The port is
also built for arm64 Android, and the recipe also as shared libraries. It also checks that the versions in both match
`COUNTLY_SDK_VERSION`.
