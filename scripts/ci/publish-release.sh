#!/usr/bin/env bash
# Prepare signed stable assets. Despite this historical filename, NEVER publish a release.
# DRY_RUN=true only writes workflow artifacts; false creates a NEW draft after all builds pass.
# Requires GH_TOKEN, GH_REPO, TAG_NAME, TARGET_SHA, ASSET_PATH, ASSET_NAME, ASSET_SHA256,
# SIGNING_ENABLED, PRODUCT_MANIFEST, DICTIONARY_TAG, DRY_RUN.
set -euo pipefail

[[ "$DRY_RUN" == true || "$DRY_RUN" == false ]]
[[ "$TAG_NAME" =~ ^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]
[[ "$TARGET_SHA" =~ ^[0-9a-f]{40}$ ]]
[[ "$SIGNING_ENABLED" == true ]] || { echo '::error::Unsigned releases are forbidden'; exit 1; }
[[ "$ASSET_NAME" == "MetasequoiaIME_Setup_${TAG_NAME}.exe" ]]
[[ "$ASSET_SHA256" =~ ^[0-9a-f]{64}$ ]]
actual_sha=$(sha256sum "$ASSET_PATH" | cut -d ' ' -f 1)
[[ "$actual_sha" == "$ASSET_SHA256" ]]

mkdir -p release-assets
cp "$ASSET_PATH" "release-assets/$ASSET_NAME"
cp "$PRODUCT_MANIFEST" release-assets/product-manifest.json
jq -n \
    --arg version "${TAG_NAME#v}" \
    --arg releaseUrl "https://github.com/${GH_REPO}/releases/tag/${TAG_NAME}" \
    --arg installerName "$ASSET_NAME" \
    --arg installerSha256 "$ASSET_SHA256" \
    '{version: $version, releaseUrl: $releaseUrl, installerName: $installerName, installerSha256: $installerSha256, signed: true}' \
    > release-assets/update.json
(
    cd release-assets
    sha256sum "$ASSET_NAME" update.json product-manifest.json > SHA256SUMS
    sha256sum --check SHA256SUMS
)

{
    printf '# %s\n\n' "$TAG_NAME"
    printf 'Signed build; awaiting manual review. This workflow never publishes.\n\n'
    printf '| Field | Value |\n| --- | --- |\n'
    printf '| Commit | `%s` |\n' "$TARGET_SHA"
    printf '| Installer | `%s` |\n' "$ASSET_NAME"
    printf '| SHA256 | `%s` |\n' "$ASSET_SHA256"
    printf '| Dictionaries | `%s` |\n' "$DICTIONARY_TAG"
    printf '\nVerify downloads with `sha256sum --check SHA256SUMS`.\n'
    printf '\nAfter review, publish as a **non-prerelease** and mark **Latest**.\n'
    printf 'Only then will `/releases/latest/download/update.json` serve this version.\n'
} > release-notes.md

if [[ "$DRY_RUN" == true ]]; then
    echo 'Dry run: no tag or release created.' >> "$GITHUB_STEP_SUMMARY"
    exit 0
fi
[[ "$GITHUB_REF" == refs/heads/yudatou/main ]] || { echo '::error::Drafts require yudatou/main'; exit 1; }
# Recheck immediately before creating metadata; fail on API errors, tags or existing drafts.
TARGET_VERSION="${TAG_NAME#v}" python scripts/ci/prepare-release.py
# No --clobber and no edit: an existing release (draft or public) must never be overwritten.
gh release create "$TAG_NAME" release-assets/* \
    --repo "$GH_REPO" --target "$TARGET_SHA" --draft --prerelease=false \
    --title "$TAG_NAME" --notes-file release-notes.md
printf 'Draft prepared: https://github.com/%s/releases\nReview before publishing.\n' "$GH_REPO" >> "$GITHUB_STEP_SUMMARY"
