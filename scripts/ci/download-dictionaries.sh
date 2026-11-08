#!/usr/bin/env bash
# Build full rime-frost from the product-locked source recipe; verify every input.
# The historical filename is retained for release/nightly/dev callers; no old pinyin asset is fetched.
set -euo pipefail
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
python "$script_dir/../product_lock.py" fetch-dictionaries --staging-root "$STAGING_ROOT"
