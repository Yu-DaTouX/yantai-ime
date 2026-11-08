#!/usr/bin/env python3
"""Cloud-only regression checks for the manual release path (no native build)."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("prepare_release", ROOT / "scripts/ci/prepare-release.py")
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class RequestTests(unittest.TestCase):
    def test_canonical_windows_versions(self):
        self.assertEqual(prepare.version_tuple("0.1.0"), (0, 1, 0))
        self.assertEqual(prepare.version_tuple("1.20.300"), (1, 20, 300))
        for value in ["0.0.1", "v0.1.0", "0.01.0", "0.1.0-beta", "1.2.65536", "1.2.3\n", "$(id)"]:
            with self.subTest(value=value), self.assertRaises(ValueError):
                prepare.version_tuple(value)

    def test_drafts_only_from_mainline(self):
        self.assertEqual(prepare.validate("0.1.0", True, "refs/heads/yt/release", [], []), "v0.1.0")
        self.assertEqual(prepare.validate("0.1.0", False, "refs/heads/yudatou/main", [], []), "v0.1.0")
        with self.assertRaises(ValueError):
            prepare.validate("0.1.0", False, "refs/heads/yt/release", [], [])

    def test_no_overwrites_or_downgrades(self):
        release = {"tag_name": "v0.1.0", "draft": False, "prerelease": False}
        for version in ["0.1.0", "0.1.1"]:
            releases = [release] if version == "0.1.0" else []
            refs = [{"ref": "refs/tags/v" + version}]
            with self.assertRaises(ValueError):
                prepare.validate(version, True, "refs/heads/yt/release", releases, refs)
        with self.assertRaises(ValueError):
            prepare.validate("0.1.0", True, "refs/heads/yt/release", [{**release, "tag_name": "v0.2.0"}], [])

    def test_components_and_prereleases_are_not_product_versions(self):
        releases = [
            {"tag_name": "translate-model-v1", "draft": False, "prerelease": True},
            {"tag_name": "v99.0.0", "draft": False, "prerelease": True},
            {"tag_name": "v98.0.0", "draft": True, "prerelease": False},
        ]
        self.assertEqual(prepare.validate("0.1.0", True, "refs/heads/yt/release", releases, []), "v0.1.0")


class AssetTests(unittest.TestCase):
    def test_dry_run_manifest_and_checksums_without_gh_writes(self):
        import hashlib
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            asset = directory / "MetasequoiaIME_Setup_v0.1.0.exe"
            asset.write_bytes(b"test signed artifact placeholder")
            product = directory / "product-manifest.json"
            product.write_text("{}", encoding="utf-8")
            # Fail if the dry-run script attempts ANY gh command.
            stub = directory / "gh"
            stub.write_text("#!/usr/bin/env bash\nexit 99\n", encoding="utf-8")
            stub.chmod(0o755)
            environment = {
                **os.environ, "PATH": str(directory) + os.pathsep + os.environ["PATH"],
                "DRY_RUN": "true", "GH_REPO": "Yu-DaTouX/yantai-ime", "TAG_NAME": "v0.1.0",
                "TARGET_SHA": "a" * 40, "ASSET_PATH": str(asset), "ASSET_NAME": asset.name,
                "ASSET_SHA256": hashlib.sha256(asset.read_bytes()).hexdigest(), "SIGNING_ENABLED": "true",
                "PRODUCT_MANIFEST": str(product), "DICTIONARY_TAG": "dict-test",
                "GITHUB_STEP_SUMMARY": str(directory / "summary"),
            }
            script = str(ROOT / "scripts/ci/publish-release.sh")
            subprocess.run(["bash", script], cwd=tmp, env=environment, check=True)
            assets = directory / "release-assets"
            manifest = json.loads((assets / "update.json").read_text())
            self.assertEqual(manifest["version"], "0.1.0")
            self.assertEqual(manifest["releaseUrl"], "https://github.com/Yu-DaTouX/yantai-ime/releases/tag/v0.1.0")
            self.assertEqual(manifest["installerName"], asset.name)
            self.assertEqual(manifest["installerSha256"], environment["ASSET_SHA256"])
            self.assertIs(manifest["signed"], True)
            subprocess.run(["sha256sum", "--check", "SHA256SUMS"], cwd=assets, check=True)
            environment["SIGNING_ENABLED"] = "false"
            self.assertNotEqual(subprocess.run(["bash", script], cwd=tmp, env=environment).returncode, 0)
            environment["SIGNING_ENABLED"] = "true"
            environment["ASSET_SHA256"] = "0" * 64
            self.assertNotEqual(subprocess.run(["bash", script], cwd=tmp, env=environment).returncode, 0)


if __name__ == "__main__":
    unittest.main()
