"""Writes the seed corpus of the value_types fuzz target to test/fuzz/corpus/value_types.

The inputs are deterministic, so running the script again gives the same bytes. The first byte of an input picks the
shape of the root node (2 or 3 is a leaf) and the next byte picks the leaf kind, as test/fuzz/value_types.cpp reads
them. The corpus files are byte-exact: never edit them by hand, run this script instead.
"""

import random
from pathlib import Path

OUT = Path(__file__).resolve().parents[2] / "test" / "fuzz" / "corpus" / "value_types"
LEAF_KINDS = 12
LEAF_SEEDS = 12
TREE_SEEDS = 4
TREE_BYTES = 96


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    rng = random.Random(5)
    seeds: dict[str, bytes] = {}
    for kind in range(LEAF_KINDS):
        payload = bytes(rng.randrange(256) for _ in range(24))
        seeds[f"leaf_{kind:02d}"] = bytes([2, kind]) + payload
    # Edge values for the leaves that depend on the bits: durations, time points and non-finite doubles.
    seeds["leaf_duration_max"] = bytes([2, 6, 2]) + b"\xff\xff\xff\xff\xff\xff\xff\x7f"
    seeds["leaf_time_point_min"] = bytes([2, 7]) + b"\x00\x00\x00\x00\x00\x00\x00\x80"
    seeds["leaf_nan_map"] = bytes([2, 3, 1, 1, 65]) + b"\x00\x00\x00\x00\x00\x00\xf8\x7f"
    seeds["empty"] = b""
    for index in range(TREE_SEEDS):
        seeds[f"tree_{index:02d}"] = bytes(rng.randrange(256) for _ in range(TREE_BYTES))
    for name, data in seeds.items():
        (OUT / name).write_bytes(data)
    print(f"wrote {len(seeds)} seeds to {OUT}")


if __name__ == "__main__":
    main()
