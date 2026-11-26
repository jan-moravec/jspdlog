"""Check that every place that states jspdlog's version agrees with project(VERSION) in CMakeLists.txt.

Usage: check-version.py [--tag vX.Y.Z]

pre-commit runs it without a tag. release.yml passes the pushed tag, which must be "v" followed by the version.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CMAKE = "CMakeLists.txt"
HEADER = "include/jspdlog/jspdlog.h"
VCPKG = "packaging/vcpkg/ports/jspdlog/vcpkg.json"
CONAN = "conanfile.py"


def find(root: Path, name: str, pattern: str) -> str:
    """Return the first group of the pattern in the file, or a marker that equals no version."""
    match = re.search(pattern, (root / name).read_text(encoding="utf-8"), re.MULTILINE)
    return match.group(1) if match else "(not found)"


def versions(root: Path) -> dict[str, str]:
    """Return the version that each file states."""
    macros = [find(root, HEADER, rf"^#define JSPDLOG_VERSION_{part} (\d+)$") for part in ("MAJOR", "MINOR", "PATCH")]
    return {
        CMAKE: find(root, CMAKE, r"^project\(\s*jspdlog\s+VERSION\s+(\d+\.\d+\.\d+)\s"),
        HEADER: ".".join(macros),
        VCPKG: find(root, VCPKG, r'"version-semver":\s*"([^"]*)"'),
        CONAN: find(root, CONAN, r'^    version = "([^"]*)"$'),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Check that jspdlog's version numbers agree.")
    parser.add_argument("--tag", help="a release tag that must name the version, for example v1.2.3")
    parser.add_argument("--root", type=Path, default=ROOT, help="the checkout to check")
    args = parser.parse_args()

    found = versions(args.root)
    expected = found[CMAKE]
    errors = [
        f"{name}: version {version}, but {CMAKE} says {expected}"
        for name, version in found.items()
        if version != expected
    ]
    if not re.fullmatch(r"\d+\.\d+\.\d+", expected):
        errors.append(f"{CMAKE}: no project(jspdlog VERSION X.Y.Z) found")
    if args.tag is not None and args.tag != f"v{expected}":
        errors.append(f"tag {args.tag} does not match version {expected}; expected v{expected}")
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print(f"version {expected} in {', '.join(found)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
