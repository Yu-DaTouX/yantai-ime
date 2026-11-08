#!/usr/bin/env python3
"""Validate a manual stable release request; never mutate GitHub metadata."""
import json
import os
import re
import subprocess
from pathlib import Path


def version_tuple(value):
    if not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", value):
        raise ValueError("Use canonical MAJOR.MINOR.PATCH without a v prefix or prerelease")
    result = tuple(map(int, value.split(".")))
    if result < (0, 1, 0) or any(part > 65535 for part in result):
        raise ValueError("Version must be >= 0.1.0 and fit Windows 16-bit version fields")
    return result


def api(path, paginate=False):
    command = ["gh", "api", path]
    if paginate:
        command += ["--paginate", "--slurp"]
    return json.loads(subprocess.check_output(command, text=True))


def validate(version, dry_run, ref, releases, refs):
    requested = version_tuple(version)
    if not dry_run and ref != "refs/heads/yudatou/main":
        raise ValueError("Draft creation requires yudatou/main; use dry_run=true on test branches")
    tag = "v" + version
    if any(release["tag_name"] == tag for release in releases) or any(
        ref["ref"] == "refs/tags/" + tag for ref in refs
    ):
        raise ValueError("Tag/release already exists; never replace an existing version")
    for release in releases:
        if release["draft"] or release["prerelease"]:
            continue
        match = re.fullmatch(r"v([0-9]+\.[0-9]+\.[0-9]+)", release["tag_name"])
        if match and requested <= version_tuple(match[1]):
            raise ValueError("Version must be newer than every published stable product release")
    return tag


def main():
    version = os.environ["TARGET_VERSION"]
    version_tuple(version)  # Reject malformed input before using it in an API path.
    repo = os.environ["GH_REPO"]
    pages = api(f"repos/{repo}/releases?per_page=100", paginate=True)
    releases = [release for page in pages for release in page]
    refs = api(f"repos/{repo}/git/matching-refs/tags/v{version}")
    tag = validate(version, os.environ["DRY_RUN"] == "true", os.environ["GITHUB_REF"], releases, refs)
    with Path(os.environ["GITHUB_OUTPUT"]).open("a", encoding="utf-8") as output:
        output.write(f"version={version}\ntag_name={tag}\n")
    print(f"Validated {tag} at {os.environ['TARGET_SHA']}; draft only, never auto-publish")


if __name__ == "__main__":
    main()
