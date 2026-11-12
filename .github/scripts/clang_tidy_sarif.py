"""
Converts clang-tidy output to a SARIF 2.1.0 file.

Usage: python3 clang_tidy_sarif.py <clang-tidy output> <output.sarif>

Reads the "<file>:<line>:<column>: <level>: <message> [<check>]" diagnostics and keeps one copy of each, because a
header's findings repeat for every source file that includes it. Notes are left out: they explain the finding above.
Paths inside the working directory are written relative to it.
"""
import json
import pathlib
import re
import sys

DIAGNOSTIC = re.compile(
    r"^(?P<file>.+?):(?P<line>\d+):(?P<column>\d+): (?P<level>warning|error): (?P<message>.*) \[(?P<check>[^\]]+)\]$")


def diagnostics(output):
    """Yields each distinct diagnostic in the clang-tidy output as a dictionary."""
    seen = set()
    for line in output.splitlines():
        match = DIAGNOSTIC.match(line.rstrip())
        if match and match.group("file", "line", "column", "check") not in seen:
            seen.add(match.group("file", "line", "column", "check"))
            yield match.groupdict()


def relative(path):
    """Returns the path relative to the working directory when it lies inside it, else unchanged."""
    try:
        return pathlib.Path(path).resolve().relative_to(pathlib.Path.cwd().resolve()).as_posix()
    except ValueError:
        return pathlib.PurePath(path).as_posix()


def help_uri(check):
    """Returns the documentation page of a clang-tidy check."""
    group, _, name = check.partition("-")
    if group == "clang" and name.startswith("analyzer-"):
        return "https://clang.llvm.org/docs/analyzer/checkers.html"
    if group == "clang" and name.startswith("diagnostic-"):
        return "https://clang.llvm.org/docs/DiagnosticsReference.html"
    return f"https://clang.llvm.org/extra/clang-tidy/checks/{group}/{name}.html"


def to_sarif(found):
    """Returns a SARIF document holding the diagnostics as one clang-tidy run."""
    checks = sorted({item["check"] for item in found})
    return {
        "$schema": "https://json.schemastore.org/sarif-2.1.0.json",
        "version": "2.1.0",
        "runs": [{
            "tool": {"driver": {
                "name": "clang-tidy",
                "informationUri": "https://clang.llvm.org/extra/clang-tidy/",
                "rules": [{"id": check, "helpUri": help_uri(check)} for check in checks],
            }},
            "results": [{
                "ruleId": item["check"],
                "level": item["level"],
                "message": {"text": item["message"]},
                "locations": [{"physicalLocation": {
                    "artifactLocation": {"uri": relative(item["file"])},
                    "region": {"startLine": int(item["line"]), "startColumn": int(item["column"])},
                }}],
            } for item in found],
        }],
    }


def main():
    """Converts the clang-tidy output file named on the command line."""
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    found = list(diagnostics(pathlib.Path(sys.argv[1]).read_text(errors="replace")))
    pathlib.Path(sys.argv[2]).write_text(json.dumps(to_sarif(found), indent=1))


if __name__ == "__main__":
    main()
