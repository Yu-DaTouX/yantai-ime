#!/usr/bin/env python3
"""Compare the pinned full profile before/after narrow fragment filters, offline.

Requires built eval_quanpin_sentence and a supplied verified sc.lm. All databases,
resources and fresh learning journals stay in --work; no installed profile is read.
"""
from __future__ import annotations

import argparse
from contextlib import closing
import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dictionary_build import frost


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--factory", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--sc-lm", type=Path, required=True)
    parser.add_argument("--evaluator", type=Path, required=True)
    parser.add_argument("--cache", type=Path, default=frost.CACHE)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    config = frost.recipe()
    frost.verify(args.factory)
    source = frost.acquire(args.cache, config)
    unfiltered = args.work / "unfiltered.db"
    if unfiltered.exists():
        unfiltered.unlink()
    original = frost.build_pinyin(source, unfiltered, config, filtered=False)
    frost.write_json(args.work / "unfiltered-build-report.json", original)
    # Freeze shipped data: do not generate a test set from the dictionary under test.
    datasets = {
        "sentences320": frost.ROOT / "engine/tests/data/quanpin_sentence_eval_300.tsv",
        "newwords268": frost.ROOT / "tests/data/frost-newwords.tsv",
        "commonwords100": frost.ROOT / "tests/data/frost-commonwords.tsv",
    }
    rows = [line.split("\t") for line in datasets["newwords268"].read_text(encoding="utf-8").splitlines()
            if line and not line.startswith("#")]
    cases = args.work / "newwords-eval.tsv"
    cases.write_text("".join(f"{''.join(py.split())}\t{word}\t{category}\n" for word, py, category in rows),
                     encoding="utf-8", newline="\n")
    datasets["newwords268"] = cases
    summary = {"recipe_sha256": frost.sha256(frost.LOCK), "builder_sha256": frost.builder_digest(),
               "evaluator_sha256": frost.sha256(args.evaluator), "sc_lm_sha256": frost.sha256(args.sc_lm),
               "rounds": 3, "warmup_rounds": 1, "execution": "BelowNormal priority, serial; CPU not exclusive",
               "datasets": {name: {"sha256": frost.sha256(path), "file": path.name}
                            for name, path in datasets.items()}, "dictionaries": {}}
    for name, db_path in [("unfiltered", unfiltered), ("filtered", args.factory / "msime.db")]:
        data = args.work / name
        data.mkdir(exist_ok=True)
        for path in data.glob("msime_user.db*"):
            path.unlink()
        for path in data.glob("msime.db*"):
            path.unlink()
        shutil.copyfile(db_path, data / "msime.db")
        for asset in ("english.db", "others.db"):
            shutil.copyfile(args.factory / asset, data / asset)
        shutil.copyfile(frost.ROOT / "engine/googlepinyinime-rev/data/dict_pinyin.dat", data / "dict_pinyin.dat")
        shutil.copyfile(frost.ROOT / "server/assets/tables/pinyin.txt", data / "pinyin.txt")
        covered, details = 0, []
        with closing(sqlite3.connect(db_path.resolve().as_uri() + "?mode=ro", uri=True)) as db:
            for word, py, category in rows:
                key = "'".join(py.split())
                hit = db.execute(f'SELECT 1 FROM "{frost.FORMAT.pinyin_table(key)}" WHERE key=? AND value=?',
                                 (key, word)).fetchone() is not None
                covered += hit
                details.append({"word": word, "pinyin": key, "category": category, "covered": hit})
        result = {"bytes": db_path.stat().st_size, "sha256": frost.sha256(db_path),
                  "covered": covered, "total": len(rows), "coverage": details, "evaluations": {}}
        for dataset, path in datasets.items():
            mode = "trigram" if dataset == "sentences320" else "off"
            env = os.environ.copy()
            env["METASEQUOIA_IME_DATA_DIR"] = str(data.resolve())
            env["LOCALAPPDATA"] = str((args.work / "isolated-local").resolve())
            command = [str(args.evaluator.resolve()), str(path.resolve()), "--mode", mode,
                       "--sc-lm", str(args.sc_lm.resolve()), "--rounds", "3", "--detail"]
            log = args.work / f"{name}-{dataset}.log"
            flags = (subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS) if os.name == "nt" else 0
            with log.open("wb") as stream:
                completed = subprocess.run(command, env=env, stdout=stream, stderr=subprocess.STDOUT,
                                           timeout=300, creationflags=flags)
            content = log.read_text(encoding="utf-8", errors="replace")
            records = [line for line in content.splitlines() if line.startswith("RESULT\t")]
            if completed.returncode or len(records) != 1:
                raise ValueError(f"Evaluation failed: {log}")
            result["evaluations"][dataset] = dict(field.split("=", 1) for field in records[0].split("\t")[1:])
            print(name, dataset, records[0], flush=True)
        summary["dictionaries"][name] = result
        frost.write_json(args.work / "results.json", summary)
    before = summary["dictionaries"]["unfiltered"]
    after = summary["dictionaries"]["filtered"]
    if before["coverage"] != after["coverage"]:
        raise ValueError("The fragment filter changed fixed new-word coverage")
    print("COVERAGE UNCHANGED", before["covered"], "/", before["total"], flush=True)


if __name__ == "__main__":
    main()
