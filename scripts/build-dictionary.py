#!/usr/bin/env python3
"""Build the reviewed full white-frost product, identically for local packaging and CI.

No sibling dictionary checkout, floating release or pypinyin dependency is used.
Raw sources and the two unchanged non-pinyin resources are SHA256-pinned in
scripts/dictionary_build/frost.lock.json; the recipe itself is pinned by product-lock.json.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sqlite3
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dictionary_build import frost
import product_lock


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=frost.OUT)
    parser.add_argument("--cache", type=Path, default=frost.CACHE)
    parser.add_argument("--if-stale", action="store_true")
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    data = product_lock.load(frost.ROOT / "product-lock.json")
    if data["dictionary"].get("mode") != "frost-source":
        raise ValueError("The product lock does not select the frost source builder")
    manifest = frost.verify(args.output) if args.verify else frost.build(args.output, args.cache, if_stale=args.if_stale)
    print(f"Verified full rime-frost {manifest['source']['commit']}: {args.output}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, sqlite3.Error) as error:
        raise SystemExit(str(error)) from error
