"""给固定的 others.db 补上符号编码表。

frost 产品固定使用 MSIME-Engine dict-v1.0.0 发布的 others.db，那一版没有 symbol_code 表，
符号混输与 Shift+S 符号模式因此查不到任何东西（引擎对旧词库静默返回空）。这里在拷贝完
support 文件之后，用 msime-dictionary 固定提交里的符号源数据加本仓的 symbol-names-extra.tsv
重建 symbol_catalog 与 symbol_code 两张表，emoji、颜文字表保持不动。
SPDX-License-Identifier: GPL-3.0-only
"""
from __future__ import annotations

import json
import sqlite3
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from product_lock_shared import download_with_retries, sha256
from dictionary_build import others_db, symbols_db, textutil

LOCK = Path(__file__).with_name("symbols.lock.json")
EXTRA = Path(__file__).with_name("symbol-names-extra.tsv")


def lock() -> dict:
    return json.loads(LOCK.read_text(encoding="utf-8"))


def _ensure(path: Path, url: str, size: int, digest: str) -> Path:
    def valid() -> bool:
        return path.is_file() and path.stat().st_size == size and sha256(path) == digest
    if not valid():
        path.parent.mkdir(parents=True, exist_ok=True)
        download_with_retries(url, path, attempts=3, timeout=90)
        if not valid():
            path.unlink(missing_ok=True)
            raise ValueError(f"Symbol input does not match reviewed SHA256: {url}")
    return path


def patch(others_path: Path, cache: Path) -> None:
    config = lock()
    source = config["dictionary"]
    root = cache / "symbols" / source["commit"]
    for relative, entry in source["files"].items():
        url = f"https://raw.githubusercontent.com/{source['repository']}/{source['commit']}/{relative}"
        _ensure(root / relative, url, entry["size"], entry["sha256"])
    override = config["pinyin_overrides"]
    override_path = _ensure(
        cache / "symbols" / "msime" / override["commit"] / override["path"],
        f"https://raw.githubusercontent.com/{override['repository']}/{override['commit']}/{override['path']}",
        override["size"], override["sha256"])

    pinyin = others_db.Pinyin(textutil.read(override_path), "pinyin-overrides.txt")
    groups = symbols_db.parse_wanxiang(textutil.read(root / symbols_db.WANXIANG))
    codex = symbols_db.parse_codex(textutil.read(root / symbols_db.CODEX))
    excluded = symbols_db.parse_unrenderable(textutil.read(root / symbols_db.UNRENDERABLE))
    names = textutil.read(root / symbols_db.READINGS)
    extra = textutil.read(EXTRA)
    readings = symbols_db.parse_readings(names.rstrip("\n") + "\n" + extra)
    catalog = symbols_db.catalog_rows(pinyin, groups, codex, excluded)
    codes = symbols_db.code_rows(groups, readings, codex, excluded)
    connection = sqlite3.connect(others_path)
    try:
        symbols_db.build(connection, catalog, codes)
    finally:
        connection.close()
    textutil.freeze(others_path)
