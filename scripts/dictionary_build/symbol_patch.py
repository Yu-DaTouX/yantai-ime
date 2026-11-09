"""给固定的 others.db 补上符号编码表。

frost 产品固定使用 MSIME-Engine dict-v1.0.0 发布的 others.db，那一版没有 symbol_code 表，
符号混输与 Shift+S 符号模式因此查不到任何东西（引擎对旧词库静默返回空）。这里在拷贝完
support 文件之后，用 msime-dictionary 固定提交里的符号源数据加本仓的 symbol-names-extra.tsv
重建 symbol_catalog 与 symbol_code 两张表，emoji、颜文字表保持不动。
SPDX-License-Identifier: GPL-3.0-only
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
import sqlite3
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from product_lock_shared import download_with_retries, sha256
from dictionary_build import others_db, symbols_db, textutil

LOCK = Path(__file__).with_name("symbols.lock.json")
EXTRA = Path(__file__).with_name("symbol-names-extra.tsv")


# others_db 的单字读音取自 pypinyin 的字表，符号表的 pinyin 列因此跟它的版本走；锁文件里的
# others_sha256 是用这一版算出来的。CI 的干净环境里没有它，缺了就按固定版本装上。
PYPINYIN = "pypinyin==0.55.0"


def ensure_pypinyin() -> None:
    if importlib.util.find_spec("pypinyin") is None:
        subprocess.run([sys.executable, "-m", "pip", "install", "--quiet", PYPINYIN], check=True)


def content_digest(path: Path) -> str:
    """others.db 的内容摘要：逐表按行排序后哈希，与 SQLite 版本导致的页布局差异无关。

    整文件的 SHA-256 在不同机器上不稳：同样的行，Python 自带的 SQLite 版本不同，VACUUM 出来的
    字节就可能不同。所以这里只认表里的内容。
    """
    digest = hashlib.sha256()
    connection = sqlite3.connect(f"{path.resolve().as_uri()}?mode=ro", uri=True)
    try:
        tables = sorted(row[0] for row in connection.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'"))
        for table in tables:
            digest.update(f"\0table:{table}\0".encode())
            rows = sorted(repr(row) for row in connection.execute(f'SELECT * FROM "{table}"'))
            for row in rows:
                digest.update(row.encode() + b"\n")
    finally:
        connection.close()
    return digest.hexdigest()


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
    ensure_pypinyin()
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
    extra = textutil.read(EXTRA).replace("\r\n", "\n")  # 检出时可能被转成 CRLF
    readings = symbols_db.parse_readings(names.rstrip("\n") + "\n" + extra)
    catalog = symbols_db.catalog_rows(pinyin, groups, codex, excluded)
    codes = symbols_db.code_rows(groups, readings, codex, excluded)
    connection = sqlite3.connect(others_path)
    try:
        symbols_db.build(connection, catalog, codes)
    finally:
        connection.close()
    textutil.freeze(others_path)
