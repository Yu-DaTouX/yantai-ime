"""Small, network-free regressions for the production full-frost builder and lock."""
import copy
from contextlib import closing
import hashlib
import json
from pathlib import Path
import sqlite3
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from dictionary_build import frost
import product_lock


class FrostDictionaryTests(unittest.TestCase):
    def fixture(self, directory):
        source = directory / "source"
        source.mkdir()
        path = source / "sample.dict.yaml"
        path.write_text("---\nname: sample\n...\n你好\tni hao\t20\n你好\tni hao\t10\n"
                        "的吧\tde ba\t0\n你你\tni ni\t0\n鱼的\tyu de\t3\n绿\tlǜ\t4\n"
                        "均订\tjunding\t50\n没有读音\n大模型\tda mo xing\t0\n", encoding="utf-8")
        config = copy.deepcopy(frost.recipe())
        config["sources"] = [{"path": path.name, "role": "dictionary", "sha256": frost.sha256(path),
                              "size": path.stat().st_size}]
        return source, config

    def test_default_recipe_matches_the_measured_full_profile(self):
        config = frost.recipe()
        packages = [e["path"] for e in config["sources"] if e["role"] == "dictionary"]
        self.assertEqual(len(packages), 29)
        self.assertEqual(sum(p.startswith("cn_dicts_cell/") for p in packages), 23)
        self.assertNotIn("cn_dicts/tencent.dict.yaml", packages)
        self.assertNotIn("cn_dicts/corrections.dict.yaml", packages)
        self.assertIn("cn_dicts/GB18030-2022.dict.yaml", packages)
        self.assertEqual(len(config["expected_content_sha256"]), 64)
        self.assertEqual(set(config["support"]["files"]), {"english.db", "others.db"})

    def test_unicode_tones_and_contract(self):
        self.assertEqual(frost.normalize("lǜ lüè nǚ"), ["lv", "lve", "nv"])
        self.assertEqual(frost.normalize("chong2'qing4"), ["chong", "qing"])
        self.assertTrue(frost.han("〇𳎜𲰹"))
        self.assertFalse(frost.han("你好A"))
        self.assertEqual(frost.FORMAT.pinyin_table("'".join(["hao"] * 8)), "tbl_others_h")

    def test_deduplication_filters_and_all_tables(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, config = self.fixture(root)
            output = root / "msime.db"
            result = frost.build_pinyin(source, output, config)
            self.assertEqual(result["rows"], 4)
            self.assertEqual(len(result["removed_fragments"]), 2)
            self.assertEqual(result["packages"]["sample.dict.yaml"]["rejected_syllable_char_mismatch"], 1)
            with closing(sqlite3.connect(output)) as connection:
                self.assertEqual(connection.execute("SELECT weight FROM tbl_2_n WHERE value='你好'").fetchone(), (100,))
                self.assertEqual(connection.execute("SELECT weight FROM tbl_2_y WHERE value='鱼的'").fetchone(), (15,))
                self.assertEqual(connection.execute("SELECT key FROM tbl_1_l WHERE value='绿'").fetchone(), ("lv",))
                self.assertEqual(connection.execute("SELECT weight FROM tbl_3_d WHERE value='大模型'").fetchone(), (1,))
                names = {x[0] for x in connection.execute("SELECT name FROM sqlite_master WHERE type='table'")}
                self.assertTrue(set(frost.FORMAT.quanpin_tables()) <= names)
                self.assertNotIn("wubi86", names)
                self.assertNotIn("japanese_lexicon", names)
                self.assertEqual(connection.execute("SELECT count(*) FROM quick_parases").fetchone(), (0,))
            again = root / "rebuilt.db"
            frost.build_pinyin(source, again, config)
            self.assertEqual(output.read_bytes(), again.read_bytes())
            with self.assertRaises(ValueError):
                frost.build_pinyin(source, output, config)

    def test_source_tampering_is_rejected_before_inserting(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, config = self.fixture(root)
            (source / "sample.dict.yaml").write_text("changed", encoding="utf-8")
            with self.assertRaises(ValueError):
                frost.build_pinyin(source, root / "msime.db", config)
            self.assertFalse((root / "msime.db").exists())

    def test_changed_product_recipe_or_support_hash_is_rejected(self):
        data = product_lock.load(ROOT / "product-lock.json")
        for key, value in [("recipe_sha256", "f" * 64), ("source_commit", "main"),
                           ("source_lock", "../recipe.json"), ("repository", "unknown/repo")]:
            altered = copy.deepcopy(data)
            altered["dictionary"][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                product_lock.validate(altered)
        altered = copy.deepcopy(data)
        altered["dictionary"]["assets"]["msime.db"] = "a" * 64
        with self.assertRaises(ValueError):
            product_lock.validate(altered)

    def test_failed_acquisition_cannot_replace_usable_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            out = root / "out"
            out.mkdir()
            (out / "msime.db").write_bytes(b"existing")
            with mock.patch.object(frost, "acquire", side_effect=ValueError("network/hash failure")):
                with self.assertRaises(ValueError):
                    frost.build(out, root / "cache")
            self.assertEqual((out / "msime.db").read_bytes(), b"existing")
            self.assertFalse((root / "out.previous").exists())

    def test_all_entrypoints_use_the_same_source_builder(self):
        local = (ROOT / "installer/Invoke-LocalTest.ps1").read_text(encoding="utf-8")
        self.assertIn("scripts\\build-dictionary.py", local)
        self.assertNotIn("pip install -q pypinyin", local)
        data = product_lock.load(ROOT / "product-lock.json")
        with tempfile.TemporaryDirectory() as temporary, mock.patch.object(frost, "build") as build, \
                mock.patch.object(product_lock, "verify_assets"):
            product_lock.fetch_dictionaries(Path(temporary), data)
            self.assertEqual(build.call_args.args[0], Path(temporary) / "MetasequoiaImeDict/out")
        self.assertEqual(data["dictionary"]["mode"], "frost-source")

    def test_frost_source_is_not_treated_as_an_unavailable_retired_repository(self):
        import subprocess
        data = product_lock.load(ROOT / "product-lock.json")
        with mock.patch.object(product_lock, "api", side_effect=subprocess.CalledProcessError(1, "gh")):
            with self.assertRaises(ValueError):
                product_lock.verify_published(data)

    def test_no_legacy_refresh_can_switch_the_default_back(self):
        with self.assertRaisesRegex(ValueError, "Frost uses reviewed source hashes"):
            product_lock.refresh("dict-v1.0.0", [])

    def test_mutating_database_and_its_manifest_together_still_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, config = self.fixture(root)
            out = root / "out"
            out.mkdir()
            result = frost.build_pinyin(source, out / "msime.db", config)
            config["expected_content_sha256"] = result["canonical_sha256"]
            config["sources"].append({"path": "LICENSE", "role": "notice", "sha256": frost.sha256(ROOT / "LICENSE"),
                                      "size": (ROOT / "LICENSE").stat().st_size})
            for name in ("english.db", "others.db"):
                (out / name).write_bytes(b"support fixture")
                config["support"]["files"][name] = frost.sha256(out / name)
            (out / "rime-frost-LICENSE.txt").write_bytes((ROOT / "LICENSE").read_bytes())
            (out / "rime-frost-NOTICE.md").write_bytes((ROOT / "docs/dictionary-frost-sources.md").read_bytes())
            manifest = {"manifest_version": 1, "profile": "desktop", "format_version": 1,
                        "engine_compatibility": {"dictionary_format": 1},
                        "source": {"repository": config["repository"], "commit": config["commit"], "dirty": False},
                        "recipe_sha256": frost.sha256(frost.LOCK), "builder_sha256": frost.builder_digest(),
                        "pinyin_content_sha256": result["canonical_sha256"],
                        "files": {name: {"sha256": frost.sha256(out / name), "size": (out / name).stat().st_size}
                                  for name in ("msime.db", "english.db", "others.db")}}
            frost.write_json(out / frost.PRODUCT.MANIFEST_NAME, manifest)
            frost.verify(out, config)
            with closing(sqlite3.connect(out / "msime.db")) as db:
                db.execute("UPDATE tbl_2_n SET weight=999999 WHERE value='你好'")
                db.commit()
            manifest["files"]["msime.db"]["sha256"] = frost.sha256(out / "msime.db")
            frost.write_json(out / frost.PRODUCT.MANIFEST_NAME, manifest)
            with self.assertRaisesRegex(ValueError, "canonical content"):
                frost.verify(out, config)

    def test_filter_does_not_reach_high_frequency_or_other_words(self):
        rows = {("de'ba", "的吧"): 1, ("de'le", "的了"): 2, ("ni'hao", "你好"): 0}
        frost.filter_rows(rows, frost.recipe())
        self.assertNotIn(("de'ba", "的吧"), rows)
        self.assertIn(("de'le", "的了"), rows)
        self.assertIn(("ni'hao", "你好"), rows)


if __name__ == "__main__":
    unittest.main(verbosity=2)
