"""Print the notes of one release from CHANGELOG.md, for the GitHub Release that release.yml drafts.

Usage: changelog-section.py X.Y.Z

GitHub shows every line break of a release body, so wrapped paragraphs and list items are joined into one line each.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REPOSITORY = "https://github.com/jan-moravec/jspdlog"

# A line that starts a block of its own and is never joined to the line before it.
BLOCK_START = re.compile(r"\s*(?:#{1,6}\s|[-*+]\s|\d+\.\s|\||>|```|~~~)")
LINK_DEFINITION = re.compile(r"\[[^\]]+\]:\s")


def section(changelog: str, version: str) -> list[str]:
    """Return the lines under the "## [version]" heading, without the heading, or an empty list."""
    lines = changelog.splitlines()
    heading = re.compile(rf"## \[{re.escape(version)}\](?:\s|$)")
    start = next((index for index, line in enumerate(lines) if heading.match(line)), None)
    if start is None:
        return []
    body: list[str] = []
    for line in lines[start + 1 :]:
        if line.startswith("## ") or LINK_DEFINITION.match(line):
            break
        body.append(line)
    return body


def unwrap(lines: list[str]) -> str:
    """Join the lines that continue a paragraph or a list item. Fenced code is copied as it is."""
    joined: list[str] = []
    fenced = False
    # Whether the next line may continue the last one: not after a blank line, a heading, a table row or a fence.
    open_block = False
    for line in lines:
        fence = line.lstrip().startswith(("```", "~~~"))
        if fenced or fence:
            fenced = fenced != fence
            joined.append(line)
            open_block = False
        elif open_block and line.strip() and not BLOCK_START.match(line):
            joined[-1] = f"{joined[-1]} {line.strip()}"
        else:
            joined.append(line.rstrip())
            open_block = bool(line.strip()) and not line.lstrip().startswith(("#", "|"))
    return "\n".join(joined).strip()


def main() -> int:
    parser = argparse.ArgumentParser(description="Print the notes of one release from CHANGELOG.md.")
    parser.add_argument("version", help="the version without the leading v, for example 1.2.3")
    parser.add_argument("--changelog", type=Path, default=ROOT / "CHANGELOG.md")
    args = parser.parse_args()

    notes = unwrap(section(args.changelog.read_text(encoding="utf-8"), args.version))
    if not notes:
        print(f"{args.changelog}: no section '## [{args.version}]' with text", file=sys.stderr)
        return 1
    link = f"**Full changelog:** [CHANGELOG.md]({REPOSITORY}/blob/v{args.version}/CHANGELOG.md)"
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    print(f"{notes}\n\n{link}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
