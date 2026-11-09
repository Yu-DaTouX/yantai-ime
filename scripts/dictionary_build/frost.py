"""Reproducible full rime-frost product. Standard library; never reads installed user data.

The source lock pins every package, supporting database and the canonical output.
No floating refs, SCEL parsing, automatic polyphone expansion or user-journal writes.
SPDX-License-Identifier: GPL-3.0-only
"""
from __future__ import annotations

import collections
from concurrent.futures import ThreadPoolExecutor
from contextlib import closing
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import sqlite3
import tempfile
import unicodedata

from product_lock_shared import download_with_retries, sha256
from . import symbol_patch

ROOT = Path(__file__).resolve().parents[2]
LOCK = Path(__file__).with_name("frost.lock.json")
OUT = ROOT / "MetasequoiaImeDict/out"
CACHE = ROOT / "MetasequoiaImeDict/cache/frost"
_spec = importlib.util.spec_from_file_location("frost_format", ROOT / "engine/contracts/dictionary/format.py")
FORMAT = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(FORMAT)
_spec = importlib.util.spec_from_file_location("frost_product", ROOT / "scripts/dictionary_product.py")
PRODUCT = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(PRODUCT)


def write_json(path: Path, value) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def recipe() -> dict:
    value = json.loads(LOCK.read_text(encoding="utf-8"))
    if (value.get("schema_version") != 1 or value.get("repository") != "gaboolic/rime-frost" or
            not re.fullmatch("[0-9a-f]{40}", value.get("commit", "")) or value.get("profile") != "full" or
            value.get("format_version") != FORMAT.FORMAT["formatVersion"] or value.get("weight_scale") != 5):
        raise ValueError("Unsupported frost recipe")
    seen = set()
    for entry in value["sources"]:
        path = PurePosixPath(entry["path"])
        if (path.is_absolute() or ".." in path.parts or not re.fullmatch(r"[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*", entry["path"]) or entry["path"] in seen):
            raise ValueError("Invalid/duplicate frost source path")
        seen.add(entry["path"])
        if (not re.fullmatch("[0-9a-f]{64}", entry["sha256"]) or
                type(entry["size"]) is not int or entry["size"] < 0 or entry["role"] not in ("dictionary", "notice")):
            raise ValueError("Invalid frost source metadata")
    if set(value["support"]["files"]) != {"english.db", "others.db"}:
        raise ValueError("Only non-pinyin legacy resources may be reused")
    return value


def builder_digest() -> str:
    digest = hashlib.sha256()
    paths = [Path(__file__), LOCK, ROOT / "scripts/build-dictionary.py",
             ROOT / "engine/contracts/dictionary/format.py", ROOT / "engine/contracts/dictionary/format.json",
             ROOT / "docs/dictionary-frost-sources.md", symbol_patch.LOCK, symbol_patch.EXTRA,
             Path(symbol_patch.__file__)]
    for path in paths:
        digest.update(path.name.encode() + b"\0" + path.read_bytes().replace(b"\r\n", b"\n") + b"\0")
    return digest.hexdigest()


def ensure(path: Path, url: str, digest: str, size: int | None = None) -> Path:
    def valid():
        return path.is_file() and (size is None or path.stat().st_size == size) and sha256(path) == digest
    if not valid():
        path.parent.mkdir(parents=True, exist_ok=True)
        download_with_retries(url, path, attempts=3, timeout=90)
        if not valid():
            path.unlink(missing_ok=True)
            raise ValueError(f"Input does not match reviewed SHA256: {url}")
    return path


def acquire(cache: Path, config: dict) -> Path:
    source = cache / config["commit"]
    def fetch(entry):
        relative = entry["path"]
        url = f"https://raw.githubusercontent.com/{config['repository']}/{config['commit']}/{relative}"
        ensure(source / relative, url, entry["sha256"], entry["size"])
    with ThreadPoolExecutor(max_workers=4) as workers:
        list(workers.map(fetch, config["sources"]))
    for name, digest in config["support"]["files"].items():
        support = config["support"]
        url = f"https://github.com/{support['repository']}/releases/download/{support['tag']}/{name}"
        ensure(cache / "support" / name, url, digest)
    return source


def han(text: str) -> bool:
    return bool(text) and all(0x3400 <= ord(ch) <= 0x4DBF or 0x4E00 <= ord(ch) <= 0x9FFF or
                             0xF900 <= ord(ch) <= 0xFAFF or 0x20000 <= ord(ch) <= 0x3FFFF or
                             ord(ch) == 0x3007 for ch in text)


def normalize(code: str) -> list[str]:
    code = code.lower().replace("u:", "v").replace("ü", "v")
    for ch in "ǖǘǚǜ":
        code = code.replace(ch, "v")
    code = "".join(ch for ch in unicodedata.normalize("NFD", code) if not unicodedata.combining(ch))
    return re.sub(r"[1-5]", "", code).replace("'", " ").split()


def parse_package(path: Path):
    stats, rows, rejects = collections.Counter(), [], []
    active = False
    for line_number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        if line.strip() == "...":
            active = True
            continue
        if not active or not line.strip() or line.lstrip().startswith("#"):
            continue
        fields = line.split("\t")
        stats["data_lines"] += 1
        reason = None
        if len(fields) < 2:
            reason = "missing_explicit_code"
        elif not han(fields[0]):
            reason = "not_pure_han"
        else:
            parts = normalize(fields[1])
            key = "'".join(parts)
            if len(parts) != len(fields[0]):
                reason = "syllable_char_mismatch"
            elif (not parts or any(not re.fullmatch("[a-z]+", part) for part in parts) or
                  parts[0][0] not in FORMAT.QUANPIN["initials"]):
                reason = "invalid_code"
            else:
                try:
                    weight = int(fields[2]) if len(fields) > 2 and fields[2].strip() else 0
                except ValueError:
                    reason = "invalid_weight"
                if reason is None:
                    if weight < 0:
                        reason = "negative_weight"
                    else:
                        rows.append((key, fields[0], weight))
                        stats["accepted_lines"] += 1
                        stats["zero_weight"] += weight == 0
        if reason:
            stats["rejected_" + reason] += 1
            if len(rejects) < 30:
                rejects.append({"line": line_number, "reason": reason, "text": line})
    if not active:
        raise ValueError(f"Missing Rime dictionary body: {path}")
    return rows, dict(stats), rejects


def filter_rows(rows: dict, config: dict) -> list[dict]:
    removed = []
    for entry in config["filters"]:
        identity = (entry["key"], entry["value"])
        if identity in rows and rows[identity] <= entry["maximum_raw_weight"]:
            removed.append({**entry, "raw_weight": rows.pop(identity)})
    return removed


def canonical_digest(connection: sqlite3.Connection) -> str:
    """Byte identity of records, not SQLite's version-dependent page layout."""
    digest = hashlib.sha256()
    for table in FORMAT.quanpin_tables():
        digest.update((table + "\n").encode())
        for row in connection.execute(f'SELECT key,jp,value,weight FROM "{table}" ORDER BY key,value'):
            digest.update((json.dumps(row, ensure_ascii=False, separators=(",", ":")) + "\n").encode())
    return digest.hexdigest()


def build_pinyin(source: Path, output: Path, config: dict, *, filtered: bool = True) -> dict:
    if output.exists():
        raise ValueError(f"Refusing to overwrite database: {output}")
    entries, packages, rejects = {}, {}, {}
    for entry in config["sources"]:
        if entry["role"] != "dictionary":
            continue
        path = source / entry["path"]
        if path.stat().st_size != entry["size"] or sha256(path) != entry["sha256"]:
            raise ValueError(f"Frost source changed: {entry['path']}")
        rows, counts, samples = parse_package(path)
        counts.update(new_unique_rows=0, duplicate_rows=0)
        for key, word, weight in rows:
            identity = (key, word)
            if identity in entries:
                counts["duplicate_rows"] += 1
                entries[identity] = max(entries[identity], weight)
            else:
                entries[identity] = weight
                counts["new_unique_rows"] += 1
        packages[entry["path"]] = counts
        rejects[entry["path"]] = samples
    removed = filter_rows(entries, config) if filtered else []
    output.parent.mkdir(parents=True, exist_ok=True)
    with closing(sqlite3.connect(output)) as connection:
        connection.execute("PRAGMA page_size=4096")
        connection.execute("PRAGMA journal_mode=DELETE")
        connection.execute("PRAGMA synchronous=OFF")
        for table in FORMAT.quanpin_tables():
            connection.execute(f'CREATE TABLE "{table}" (key TEXT,jp TEXT,value TEXT,weight INTEGER DEFAULT 0)')
        groups = collections.defaultdict(list)
        for (key, word), raw in entries.items():
            weight = max(1, min(100000000, raw * config["weight_scale"]))
            groups[FORMAT.pinyin_table(key)].append((key, "".join(part[0] for part in key.split("'")), word, weight))
        for table, rows in groups.items():
            connection.executemany(f'INSERT INTO "{table}" VALUES (?,?,?,?)', rows)
        for table in FORMAT.quanpin_tables():
            suffix = table.removeprefix("tbl_")
            connection.execute(f'CREATE INDEX idx_key_{suffix} ON "{table}" (key)')
            connection.execute(f'CREATE INDEX idx_jp_{suffix} ON "{table}" (jp)')
        connection.execute("CREATE TABLE quick_parases (key TEXT NOT NULL,value TEXT NOT NULL,"
                           "weight INTEGER NOT NULL DEFAULT 0,UNIQUE(key,value))")
        connection.execute("CREATE INDEX idx_key_quick_parases ON quick_parases(key)")
        connection.execute("CREATE TABLE latin_words (key TEXT NOT NULL,value TEXT NOT NULL,weight INTEGER NOT NULL,"
                           "exact_only INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(key,value))")
        connection.execute("ANALYZE")
        connection.commit()
        connection.execute("VACUUM")
        if connection.execute("PRAGMA integrity_check").fetchone() != ("ok",):
            raise ValueError("Invalid built pinyin dictionary")
        content_id = canonical_digest(connection)
    return {"rows": len(entries), "packages": packages, "removed_fragments": removed,
            "reject_samples": rejects, "weight_rule": "max(1,min(100000000,raw_weight*5))",
            "canonical_sha256": content_id, "sha256": sha256(output), "bytes": output.stat().st_size}


def verify(directory: Path, config: dict | None = None) -> dict:
    config = config or recipe()
    manifest = PRODUCT.verify_product(directory, "desktop")
    if manifest.get("source") != {"repository": config["repository"], "commit": config["commit"], "dirty": False}:
        raise ValueError("Wrong frost product provenance")
    if manifest.get("recipe_sha256") != sha256(LOCK) or manifest.get("builder_sha256") != builder_digest():
        raise ValueError("Frost recipe/builder changed; rebuild required")
    notices = {"rime-frost-LICENSE.txt": next(e["sha256"] for e in config["sources"] if e["path"] == "LICENSE"),
               "rime-frost-NOTICE.md": sha256(ROOT / "docs/dictionary-frost-sources.md")}
    for name, digest in notices.items():
        if sha256(directory / name) != digest:
            raise ValueError(f"Missing/changed frost attribution: {name}")
    if manifest.get("pinyin_content_sha256") != config.get("expected_content_sha256"):
        raise ValueError("Manifest canonical identity differs from reviewed content")
    for name, digest in config["support"]["files"].items():
        # others.db 在 support 原件之上补了符号表（symbol_patch），改按补丁后的内容摘要核对。
        if name == "others.db":
            if symbol_patch.content_digest(directory / name) != symbol_patch.lock()["others_content_sha256"]:
                raise ValueError(f"Supporting dictionary changed: {name}")
            continue
        if sha256(directory / name) != digest:
            raise ValueError(f"Supporting dictionary changed: {name}")
    with closing(sqlite3.connect((directory / "msime.db").resolve().as_uri() + "?mode=ro", uri=True)) as connection:
        names = {row[0] for row in connection.execute("SELECT name FROM sqlite_master WHERE type='table'")}
        allowed = set(FORMAT.quanpin_tables()) | {"quick_parases", "latin_words", "sqlite_stat1", "sqlite_stat4"}
        if not set(FORMAT.quanpin_tables()) <= names or names - allowed:
            raise ValueError("Missing pinyin tables or retired/unknown tables present")
        if any(connection.execute(f'SELECT 1 FROM "{table}" LIMIT 1').fetchone() for table in ("quick_parases", "latin_words")):
            raise ValueError("Factory dictionary contains personal/mixed data")
        if connection.execute("PRAGMA integrity_check").fetchone() != ("ok",):
            raise ValueError("Invalid frost SQLite product")
        expected = config.get("expected_content_sha256")
        if not expected or canonical_digest(connection) != expected:
            raise ValueError("Pinyin records do not match reviewed canonical content")
    return manifest


def build(output: Path = OUT, cache: Path = CACHE, *, if_stale: bool = False) -> dict:
    output, cache = output.resolve(), cache.resolve()
    config = recipe()
    # Do not accidentally target a live profile, including via relative paths/junctions.
    import os
    live = Path(os.environ.get("LOCALAPPDATA", str(ROOT))) / "metasequoiaime"
    if output == live.resolve() or output.is_relative_to(live.resolve()):
        raise ValueError("Build output must not be an installed user data directory")
    if if_stale and (output / PRODUCT.MANIFEST_NAME).is_file():
        try:
            return verify(output, config)
        except (ValueError, OSError, sqlite3.Error):
            pass
    source = acquire(cache, config)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="incoming-frost-", dir=output.parent) as temporary:
        incoming = Path(temporary)
        result = build_pinyin(source, incoming / "msime.db", config)
        expected = config.get("expected_content_sha256")
        if expected and result["canonical_sha256"] != expected:
            raise ValueError("Built pinyin content differs from reviewed lock")
        write_json(incoming / "frost-build-report.json", result)
        for name in config["support"]["files"]:
            shutil.copyfile(cache / "support" / name, incoming / name)
        symbol_patch.patch(incoming / "others.db", cache)
        shutil.copyfile(source / "LICENSE", incoming / "rime-frost-LICENSE.txt")
        shutil.copyfile(ROOT / "docs/dictionary-frost-sources.md", incoming / "rime-frost-NOTICE.md")
        manifest = {"manifest_version": 1, "profile": "desktop", "format_version": 1,
                    "engine_compatibility": {"dictionary_format": 1}, "sqlite_journal_mode": "delete",
                    "source": {"repository": config["repository"], "commit": config["commit"], "dirty": False},
                    "recipe_sha256": sha256(LOCK), "builder_sha256": builder_digest(),
                    "pinyin_content_sha256": result["canonical_sha256"],
                    "features": ["pinyin", "quick_phrases", "english", "emoji", "kaomoji", "symbols"],
                    "source_packages": config["sources"], "supporting_dictionaries": config["support"],
                    "files": {name: {"sha256": sha256(incoming / name), "size": (incoming / name).stat().st_size}
                              for name in sorted(PRODUCT.DESKTOP_FILES)}}
        write_json(incoming / PRODUCT.MANIFEST_NAME, manifest)
        if expected:
            verify(incoming, config)
        shipping = sorted(path.name for path in incoming.iterdir())
        (incoming / "SHA256SUMS.txt").write_text("".join(f"{sha256(incoming / name)}  {name}\n" for name in shipping),
                                                  encoding="utf-8", newline="\n")
        # A whole-directory handoff with rollback, never update live database files one at a time.
        backup = output.with_name(output.name + ".previous")
        if backup.exists():
            raise ValueError(f"Previous generation already exists; review before rebuilding: {backup}")
        if output.exists():
            output.rename(backup)
        try:
            shutil.copytree(incoming, output)
        except BaseException:
            if output.exists():
                shutil.rmtree(output)
            if backup.exists():
                backup.rename(output)
            raise
        if backup.exists():
            shutil.rmtree(backup)
    return manifest
