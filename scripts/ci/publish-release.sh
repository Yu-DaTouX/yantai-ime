#!/usr/bin/env bash
# Attach the installer to the selected release, append the artifact details to its notes and publish.
#
# The release-please tag is the stable version selected for a future manual release. An automatic
# build gets its own SemVer prerelease tag so that the prerelease and the eventual stable release
# can coexist: v0.6.2-beta is the automatic build, while v0.6.2 remains the stable draft.
#
# The prerelease flag means "automatic and uncurated" here, not "beta". The product being 内测 is
# stated in the notes and in docs/installation.md.
#
# Requires GH_TOKEN, GH_REPO, TAG_NAME, ASSET_PATH, ASSET_NAME, ASSET_SHA256, SIGNING_ENABLED,
# DICTIONARY_TAG, PRODUCT_MANIFEST, RELEASE_TRIGGER and TARGET_SHA.
#
# Also generates and uploads two derived assets next to the installer: update.json, which the
# client's update checker polls, and <installer>.sha256 for manual verification.
set -euo pipefail

if [[ "$RELEASE_TRIGGER" == push ]]; then
    release_tag="${TAG_NAME}-beta"
    title="${release_tag}（自动构建）"
    banner='本版本由 CI 在合并到 `main` 后自动构建发布。'

    if release_json=$(gh release view "$release_tag" --repo "$GH_REPO" --json isDraft,isPrerelease,targetCommitish 2>/dev/null); then
        test "$(jq -er .isDraft <<< "$release_json")" = false
        test "$(jq -er .isPrerelease <<< "$release_json")" = true
        test "$(jq -er .targetCommitish <<< "$release_json")" = "$TARGET_SHA"
    else
        gh release create "$release_tag" \
            --repo "$GH_REPO" \
            --target "$TARGET_SHA" \
            --title "$title" \
            --notes "$banner" \
            --prerelease
    fi

    channel=(--prerelease)
else
    release_tag="$TAG_NAME"
    title="$TAG_NAME"
    banner=''
    channel=(--prerelease=false --latest)
fi

# The update manifest the client polls:
# https://github.com/<repo>/releases/latest/download/update.json
#
# Fields are fixed by server/src/settings/update_check.cpp; anything missing or malformed makes
# the client treat the release as "no update" rather than as an error:
#   version          MAJOR.MINOR.PATCH with no prerelease label (0.x counts as a release)
#   releaseUrl       https://github.com/<repo>/releases/tag/<tag>, and <tag> must parse to version
#   installerName    the installer asset name (MetasequoiaIME_Setup_v<version>[-unsigned].exe)
#   installerSha256  64 lowercase hex characters
#   signed           false for an -unsigned build; the about page shows a warning then
#
# The manifest points at the stable tag on purpose: TAG_NAME with no -beta suffix. An automatic
# build therefore carries an update.json that names the stable release it belongs to. That is
# harmless because /releases/latest/ only resolves to a non-prerelease release, and automatic
# builds are published as prereleases.
manifest_version="${TAG_NAME#v}"
manifest_release_url="https://github.com/${GH_REPO}/releases/tag/${TAG_NAME}"
manifest_signed=false
if [[ "$SIGNING_ENABLED" == true ]]; then
    manifest_signed=true
fi
manifest_file="release-assets/update.json"
checksum_file="release-assets/${ASSET_NAME}.sha256"
mkdir -p release-assets
jq -n \
    --arg version "$manifest_version" \
    --arg releaseUrl "$manifest_release_url" \
    --arg installerName "$ASSET_NAME" \
    --arg installerSha256 "$ASSET_SHA256" \
    --argjson signed "$manifest_signed" \
    '{version: $version, releaseUrl: $releaseUrl, installerName: $installerName, installerSha256: $installerSha256, signed: $signed}' \
    > "$manifest_file"

# Same checksum in sha256sum's format, so a user can verify the download by hand.
printf '%s  %s\n' "$ASSET_SHA256" "$ASSET_NAME" > "$checksum_file"

gh release upload "$release_tag" "$ASSET_PATH" "$PRODUCT_MANIFEST" "$manifest_file" "$checksum_file" \
    --repo "$GH_REPO" --clobber
gh release view "$release_tag" --repo "$GH_REPO" --json body --jq .body > notes.md
{
    if [[ -n "$banner" ]]; then
        printf '\n> %s\n' "$banner"
    fi
    printf '\n---\n\n'
    printf '| Field | Value |\n| --- | --- |\n'
    printf '| Installer | `%s` |\n' "$ASSET_NAME"
    printf '| SHA256 | `%s` |\n' "$ASSET_SHA256"
    printf '| Dictionaries | `%s` |\n' "$DICTIONARY_TAG"
    printf '| Build inputs | `product-manifest.json` (attached and installed) |\n'
    printf '\n[Code signing policy](https://github.com/%s/blob/develop/docs/code-signing-policy.md)\n' "$GH_REPO"
    if [[ "$SIGNING_ENABLED" != true ]]; then
        printf '\nThis build is **unsigned**. Windows warns on launch, and `uiAccess` does not take effect, so the candidate window cannot float over elevated applications.\n'
    fi
} >> notes.md

gh release edit "$release_tag" --repo "$GH_REPO" --draft=false "${channel[@]}" --title "$title" --notes-file notes.md
echo "Published $release_tag as '$title' (${channel[*]}) with $ASSET_NAME"
