"""Check the comments in jspdlog's public headers against the rules in AGENTS.md.

Every header under include/ must start with a /** */ file comment that has an @brief, placed above #pragma once.
Comments must not contain backticks and must use /** */ or //, not the triple-slash form.

Invoked from pre-commit.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER_ROOT = ROOT / "include"
COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)


def check(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    problems = []
    head = text.lstrip()
    pragma = head.find("#pragma once")
    if pragma < 0 or not head.startswith("/**") or "@brief" not in head[:pragma]:
        problems.append("1: header does not start with a /** */ file comment with @brief above #pragma once")
    for match in COMMENT_RE.finditer(text):
        comment = match.group()
        if comment.startswith("///"):
            line = text.count("\n", 0, match.start()) + 1
            problems.append(f"{line}: triple-slash comment, use /** */")
        if "`" in comment:
            line = text.count("\n", 0, match.start() + comment.index("`")) + 1
            problems.append(f"{line}: backtick in comment")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="*", type=Path)
    args = parser.parse_args()
    files = args.files or sorted(HEADER_ROOT.rglob("*.h"))
    total = 0
    for path in files:
        problems = check(path.resolve())
        total += len(problems)
        shown = path.resolve().relative_to(ROOT).as_posix()
        for problem in problems:
            print(f"{shown}:{problem}")
    print(f"check-docs: {total} problem(s) in {len(files)} file(s)")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
