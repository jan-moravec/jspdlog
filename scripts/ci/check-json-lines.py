"""Check that every non-empty line of a file is a JSON object. Used by CI on the examples' output."""

from __future__ import annotations

import json
import sys
from pathlib import Path


def main() -> int:
    path = Path(sys.argv[1])
    lines = [line for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
    for number, line in enumerate(lines, start=1):
        try:
            value = json.loads(line)
        except json.JSONDecodeError as error:
            print(f"{path}:{number}: not valid JSON ({error}): {line}")
            return 1
        if not isinstance(value, dict):
            print(f"{path}:{number}: not a JSON object: {line}")
            return 1
    print(f"{path}: {len(lines)} valid JSON line(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
