#!/usr/bin/env python3
"""Real dictionary-build -> replay upgrade test. Uses only synthetic temporary user data.

Run by scripts/ci/test-server.ps1 after building the real Windows helpers.
No Server/Watchdog/TSF registration or installed profile is accessed.
"""
from __future__ import annotations

import argparse
from contextlib import closing
import importlib.util
import json
from pathlib import Path
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import os

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from dictionary_build import frost


def run(replay: Path, factory: Path, builder: Path | None = None):
    frost.verify(factory)
    spec = importlib.util.spec_from_file_location("upgrade_pack", ROOT / "scripts/pack-dictionary-source.py")
    pack = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pack)
    builder = builder or replay.with_name("MetasequoiaImeDictionaryBuild.exe")
    if not replay.is_file() or not builder.is_file():
        raise ValueError("Build and replay executables must exist")
    with tempfile.TemporaryDirectory(prefix="frost-upgrade-") as temporary:
        root = Path(temporary)
        data, dumps, backup = root / "data", root / "dumps", root / "backup"
        for path in (data, dumps, backup):
            path.mkdir()
        for name in ("msime.db", "english.db", "others.db"):
            pack.dump_database(factory / name, dumps / (Path(name).stem + ".dump"))
            shutil.copyfile(factory / name, data / name)
        shutil.copyfile(factory / "dictionary-manifest.json", data / "dictionary-manifest.json")
        # A representative previous factory generation contained these now-retired tables.
        with closing(sqlite3.connect(data / "msime.db")) as previous:
            previous.executescript("CREATE TABLE wubi86(key TEXT,value TEXT,weight INTEGER);"
                                   "INSERT INTO wubi86 VALUES('aaa','旧五笔',1);"
                                   "CREATE TABLE japanese_lexicon(code TEXT,value TEXT,weight INTEGER);")
        (data / "config.toml").write_bytes(b"# keep user's preferences\n")
        (data / "stats.db").write_bytes(b"synthetic statistics sentinel")
        journal = sqlite3.connect(data / "msime_user.db")
        journal.execute("PRAGMA journal_mode=WAL")
        journal.execute("PRAGMA wal_autocheckpoint=0")
        journal.executescript("CREATE TABLE user_dictionary_operations(dictionary TEXT NOT NULL,key TEXT NOT NULL,"
                             "value TEXT NOT NULL,operation TEXT NOT NULL,weight INTEGER NOT NULL DEFAULT 0,"
                             "display TEXT NOT NULL DEFAULT '',user_inserted INTEGER NOT NULL DEFAULT 0,"
                             "weight_edited INTEGER NOT NULL DEFAULT 0,updated_at INTEGER NOT NULL DEFAULT 1,"
                             "PRIMARY KEY(dictionary,key,value));"
                             "CREATE TABLE dictionary_state(name TEXT PRIMARY KEY,value TEXT NOT NULL);"
                             "INSERT INTO dictionary_state VALUES('replayed_dictionary_sha256','old-generation');"
                             "CREATE TABLE candidate_selection_state(context_key TEXT,entry_key TEXT,value TEXT,selection_count INTEGER);"
                             "INSERT INTO candidate_selection_state VALUES('probe','ni','你',9);"
                             "CREATE TABLE fixed_candidate_positions(context_key TEXT,entry_key TEXT,value TEXT,position INTEGER);"
                             "INSERT INTO fixed_candidate_positions VALUES('probe','ni','你',2);")
        entries = [
            ("pinyin", "shui'shan'shi'yan", "水杉实验", "upsert", 23456, "", 1, 0),
            ("pinyin", "shui'shan'bai'shuang'ci'ku'hao", "水杉白霜词库好", "upsert", 17000, "", 1, 0),
            ("pinyin", "shui'shan'bai'shuang'ci'ku'hao'a", "水杉白霜词库好啊", "upsert", 18000, "", 1, 0),
            ("pinyin", "shui'shan'bai'shuang'ci'ku'hao'a'ya", "水杉白霜词库好啊呀", "upsert", 19000, "", 1, 0),
            ("pinyin", "shi", "是", "upsert", 1, "", 0, 0),
            ("pinyin", "wo", "我", "upsert", 99999999, "", 0, 0),
            ("pinyin", "ni", "你", "upsert", 123, "", 0, 1),
            ("pinyin", "de", "的", "delete", 0, "", 0, 0),
            # A personally added fragment must NOT be affected by factory filtering.
            ("pinyin", "de'ba", "的吧", "upsert", 8000, "", 1, 0),
            ("quick", "zz", "自造短语", "upsert", 50000, "", 1, 0),
            ("english", "frostprobe", "frostprobe", "upsert", 50000, "FrostProbe", 1, 0),
            ("wubi", "aaa", "旧五笔", "upsert", 1000, "", 1, 0),
        ]
        journal.executemany("INSERT INTO user_dictionary_operations"
                            "(dictionary,key,value,operation,weight,display,user_inserted,weight_edited)"
                            " VALUES (?,?,?,?,?,?,?,?)", entries)
        journal.commit()
        assert (data / "msime_user.db-wal").stat().st_size > 0, "test must include uncheckpointed learning"
        # Backup main + WAL + SHM while the synthetic writer is quiescent, exactly as documented.
        for name in ("msime_user.db", "msime_user.db-wal", "msime_user.db-shm", "msime.db", "dictionary-manifest.json"):
            shutil.copyfile(data / name, backup / name)
        env = os.environ.copy()
        env["METASEQUOIA_IME_DATA_DIR"] = str(data)
        env["LOCALAPPDATA"] = str(root / "isolated-local")
        commands = [[str(builder.resolve()), "--source", str(dumps), "--data-dir", str(data)],
                    [str(replay.resolve()), "--data-dir", str(data)]]
        for command in commands:
            result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
            print(result.stdout.decode("utf-8", errors="replace"))
            if result.returncode:
                raise AssertionError(f"Helper exited {result.returncode}: {command}")
        with closing(sqlite3.connect(data / "msime.db")) as actual, \
                closing(sqlite3.connect(factory / "msime.db")) as shipped:
            names = {x[0] for x in actual.execute("SELECT name FROM sqlite_master WHERE type='table'")}
            assert "wubi86" not in names and "japanese_lexicon" not in names
            for kind, key, word, operation, weight, display, inserted, edited in entries:
                if kind != "pinyin":
                    continue
                row = actual.execute(f'SELECT weight FROM "{frost.FORMAT.pinyin_table(key)}" WHERE key=? AND value=?',
                                     (key, word)).fetchone()
                if operation == "delete":
                    assert row is None, "deletion tombstone was not replayed"
                elif key == "shi":
                    expected = shipped.execute("SELECT weight FROM tbl_1_s WHERE key='shi' AND value='是'").fetchone()
                    assert row == expected, "old low automatic weight must yield to new factory"
                else:
                    assert row == (weight,), (key, word, row, weight)
            assert actual.execute("SELECT value FROM quick_parases WHERE key='zz'").fetchone() == ("自造短语",)
            assert actual.execute("PRAGMA integrity_check").fetchone() == ("ok",)
        assert journal.execute("SELECT count(*) FROM user_dictionary_operations").fetchone() == (len(entries),)
        assert journal.execute("SELECT selection_count FROM candidate_selection_state").fetchone() == (9,)
        assert journal.execute("SELECT position FROM fixed_candidate_positions").fetchone() == (2,)
        assert journal.execute("SELECT value FROM dictionary_state WHERE name='replayed_dictionary_sha256'").fetchone()[0] == \
            json.loads((factory / "dictionary-manifest.json").read_text(encoding="utf-8"))["files"]["msime.db"]["sha256"]
        journal.close()
        with closing(sqlite3.connect(data / "english.db")) as english:
            assert english.execute("SELECT weight FROM english_words WHERE word='frostprobe' AND display='FrostProbe'").fetchone() == (50000,)
        assert (data / "config.toml").read_bytes() == b"# keep user's preferences\n"
        assert (data / "stats.db").read_bytes() == b"synthetic statistics sentinel"
        again = subprocess.run(commands[1], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
        assert again.returncode == 0, again.stdout
        print("UPGRADE OK: WAL learning, user words/weights/deletes, manual edits, 7/8/9-syllable rows, "
              "quick/English, selection/fixed state and preferences preserved; retired tables absent; replay idempotent")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--replay", type=Path, required=True)
    parser.add_argument("--factory", type=Path, required=True)
    parser.add_argument("--build", type=Path)
    args = parser.parse_args()
    run(args.replay, args.factory, args.build)
