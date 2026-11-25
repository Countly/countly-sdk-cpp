# Breakpad crash handler client (vendored)

This directory contains the in-process crash handler client of Google
Breakpad for Linux and Windows, and the linux-syscall-support header it needs.
The files are copied verbatim; only the client sources and the headers they
include are kept (91 files).

- **Breakpad:** https://chromium.googlesource.com/breakpad/breakpad, commit `f9f68ccdb33a188e879b5fc0a58d23e84c9b9ae9`
- **linux-syscall-support:** https://chromium.googlesource.com/linux-syscall-support, commit `29164a80da4d41134950d76d55199ea33fbb9613`
- **License:** BSD 3-Clause (`LICENSE` for Breakpad, `LICENSE.lss` for
  linux-syscall-support)

The files are compiled only when `COUNTLY_USE_BREAKPAD=ON`. `sources.cmake`
lists the sources for each platform.

## Updating

Run `python3 scripts/update_breakpad.py <breakpad-commit> <lss-commit>`. The
script fetches both repositories at the given commits and replaces the files
here, including `sources.cmake` and this README.
