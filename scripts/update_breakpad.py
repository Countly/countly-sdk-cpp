#!/usr/bin/env python3
"""Update the vendored Breakpad crash handler client in vendor/breakpad/.

Usage: python3 scripts/update_breakpad.py <breakpad-commit> <lss-commit>
       python3 scripts/update_breakpad.py --breakpad-dir <checkout> --lss-dir <checkout>

Fetches Breakpad (https://chromium.googlesource.com/breakpad/breakpad) and
linux-syscall-support (https://chromium.googlesource.com/linux-syscall-support)
at the given commits, or reads local checkouts, and replaces vendor/breakpad/
with the client sources for Linux and Windows, every header they include, and
the licenses. It also writes vendor/breakpad/sources.cmake, the source lists
that CMakeLists.txt compiles when COUNTLY_USE_BREAKPAD is ON, and
vendor/breakpad/README.md with the commits.

Requires git (when commits are given) and the Python standard library.
"""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
VENDOR_DIR = REPO_ROOT / "vendor" / "breakpad"
BREAKPAD_URL = "https://chromium.googlesource.com/breakpad/breakpad"
LSS_URL = "https://chromium.googlesource.com/linux-syscall-support"

# The in-process client of Breakpad's own Linux build (Makefile.am,
# libbreakpad_client.a), without the crash generation server and the core
# file dumper, which only out-of-process and offline tools use.
LINUX_SOURCES = [
    "src/client/linux/crash_generation/crash_generation_client.cc",
    "src/client/linux/dump_writer_common/thread_info.cc",
    "src/client/linux/dump_writer_common/ucontext_reader.cc",
    "src/client/linux/handler/exception_handler.cc",
    "src/client/linux/handler/minidump_descriptor.cc",
    "src/client/linux/log/log.cc",
    "src/client/linux/microdump_writer/microdump_writer.cc",
    "src/client/linux/minidump_writer/linux_dumper.cc",
    "src/client/linux/minidump_writer/linux_ptrace_dumper.cc",
    "src/client/linux/minidump_writer/minidump_writer.cc",
    "src/client/linux/minidump_writer/pe_file.cc",
    "src/client/minidump_file_writer.cc",
    "src/common/convert_UTF.cc",
    "src/common/md5.cc",
    "src/common/string_conversion.cc",
    "src/common/linux/elfutils.cc",
    "src/common/linux/file_id.cc",
    "src/common/linux/guid_creator.cc",
    "src/common/linux/linux_libc_support.cc",
    "src/common/linux/memory_mapped_file.cc",
    "src/common/linux/safe_readlink.cc",
]

# Needed only where the C library has no getcontext(), such as musl.
LINUX_GETCONTEXT_SOURCE = "src/common/linux/breakpad_getcontext.S"

# The in-process exception handler of Breakpad's Windows client.
WINDOWS_SOURCES = [
    "src/client/windows/crash_generation/crash_generation_client.cc",
    "src/client/windows/handler/exception_handler.cc",
    "src/common/windows/guid_string.cc",
]

INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)


def write_text(path, text):
    """Writes a text file with LF line endings on every platform."""
    with open(path, "w", encoding="utf-8", newline="\n") as file:
        file.write(text)


def run(arguments, cwd=None):
    """Runs a command and stops the script when it fails."""
    result = subprocess.run(arguments, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.returncode != 0:
        sys.exit(f"error: {' '.join(arguments)} failed:\n{result.stdout}")
    return result.stdout.strip()


def checkout(url, commit, target):
    """Fetches one commit of a repository into an empty directory."""
    target.mkdir(parents=True)
    run(["git", "init", "-q"], cwd=target)
    run(["git", "fetch", "-q", "--depth", "1", url, commit], cwd=target)
    run(["git", "checkout", "-q", "FETCH_HEAD"], cwd=target)


def head_commit(directory):
    """The commit a local checkout is on."""
    return run(["git", "rev-parse", "HEAD"], cwd=directory)


def resolve_include(include, including_file, breakpad_root):
    """The Breakpad file an include names, or None when it is not part of Breakpad."""
    for base in (breakpad_root / "src", including_file.parent):
        candidate = (base / include).resolve()
        if candidate.is_file() and breakpad_root.resolve() in candidate.parents:
            return candidate
    return None


def collect_files(sources, breakpad_root):
    """The sources and every Breakpad header they include, directly or not."""
    pending = [(breakpad_root / source).resolve() for source in sources]
    found = set()
    while pending:
        current = pending.pop()
        if current in found:
            continue
        if not current.is_file():
            sys.exit(f"error: {current} does not exist in the Breakpad checkout")
        found.add(current)
        text = current.read_text(encoding="utf-8", errors="replace")
        for include in INCLUDE_PATTERN.findall(text):
            if include == "third_party/lss/linux_syscall_support.h":
                continue
            resolved = resolve_include(include, current, breakpad_root)
            if resolved is not None:
                pending.append(resolved)
    return found


def write_sources_cmake(path):
    """Writes the source lists that CMakeLists.txt compiles."""
    def block(name, sources):
        lines = "\n".join(f"  ${{CMAKE_CURRENT_LIST_DIR}}/{source}" for source in sources)
        return f"set({name}\n{lines})\n"

    write_text(
        path,
        "# Written by scripts/update_breakpad.py. Do not edit by hand.\n"
        + block("COUNTLY_BREAKPAD_LINUX_SOURCES", LINUX_SOURCES)
        + f"set(COUNTLY_BREAKPAD_LINUX_GETCONTEXT_SOURCE ${{CMAKE_CURRENT_LIST_DIR}}/{LINUX_GETCONTEXT_SOURCE})\n"
        + block("COUNTLY_BREAKPAD_WINDOWS_SOURCES", WINDOWS_SOURCES))


def write_readme(path, breakpad_commit, lss_commit, file_count):
    """Writes the README that records where the files come from."""
    write_text(path, f"""# Breakpad crash handler client (vendored)

This directory contains the in-process crash handler client of Google
Breakpad for Linux and Windows, and the linux-syscall-support header it needs.
The files are copied verbatim; only the client sources and the headers they
include are kept ({file_count} files).

- **Breakpad:** {BREAKPAD_URL}, commit `{breakpad_commit}`
- **linux-syscall-support:** {LSS_URL}, commit `{lss_commit}`
- **License:** BSD 3-Clause (`LICENSE` for Breakpad, `LICENSE.lss` for
  linux-syscall-support)

The files are compiled only when `COUNTLY_USE_BREAKPAD=ON`. `sources.cmake`
lists the sources for each platform.

## Updating

Run `python3 scripts/update_breakpad.py <breakpad-commit> <lss-commit>`. The
script fetches both repositories at the given commits and replaces the files
here, including `sources.cmake` and this README.
""")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("breakpad_commit", nargs="?", help="Breakpad commit to vendor")
    parser.add_argument("lss_commit", nargs="?", help="linux-syscall-support commit to vendor")
    parser.add_argument("--breakpad-dir", help="use this Breakpad checkout instead of fetching")
    parser.add_argument("--lss-dir", help="use this linux-syscall-support checkout instead of fetching")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as temporary:
        if args.breakpad_dir:
            breakpad_root = Path(args.breakpad_dir)
        elif args.breakpad_commit:
            breakpad_root = Path(temporary) / "breakpad"
            checkout(BREAKPAD_URL, args.breakpad_commit, breakpad_root)
        else:
            sys.exit("error: give a Breakpad commit or --breakpad-dir")
        if args.lss_dir:
            lss_root = Path(args.lss_dir)
        elif args.lss_commit:
            lss_root = Path(temporary) / "lss"
            checkout(LSS_URL, args.lss_commit, lss_root)
        else:
            sys.exit("error: give a linux-syscall-support commit or --lss-dir")

        breakpad_commit = head_commit(breakpad_root)
        lss_commit = head_commit(lss_root)
        files = collect_files(LINUX_SOURCES + [LINUX_GETCONTEXT_SOURCE] + WINDOWS_SOURCES, breakpad_root)

        if VENDOR_DIR.exists():
            shutil.rmtree(VENDOR_DIR)
        for file in sorted(files):
            target = VENDOR_DIR / file.relative_to(breakpad_root.resolve())
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(file, target)
        lss_target = VENDOR_DIR / "src" / "third_party" / "lss" / "linux_syscall_support.h"
        lss_target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(lss_root / "linux_syscall_support.h", lss_target)
        shutil.copyfile(breakpad_root / "LICENSE", VENDOR_DIR / "LICENSE")
        shutil.copyfile(lss_root / "LICENSE", VENDOR_DIR / "LICENSE.lss")

        file_count = len(files) + 1
        write_sources_cmake(VENDOR_DIR / "sources.cmake")
        write_readme(VENDOR_DIR / "README.md", breakpad_commit, lss_commit, file_count)

    print(f"Vendored Breakpad {breakpad_commit} and linux-syscall-support {lss_commit}: {file_count} files.")


if __name__ == "__main__":
    main()
