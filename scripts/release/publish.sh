#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Publishes a FrameWeave GitHub release from the built files in dist/ (release notes from
# packaging/release-notes/<tag>.md plus the install, OBS and licence sections below).
# Used by build-windows.yml (release job) and release-from-run.yml (files of an earlier build).
# Env: TAG (vX.Y.Z), FULL_RELEASE (true/false), GH_TOKEN, GITHUB_REPOSITORY, GITHUB_SHA.
# NOTES_ONLY=true (release-from-run.yml): only the words on the page of a release that is already out
# are written again. No file is uploaded, nothing is signed, the checksum list and its signature stay
# as they are, so the updater sees no change.
set -euo pipefail
build_sha=${BUILD_SHA:-$GITHUB_SHA}
source_dir=${BUILD_SOURCE:-.}
[[ $TAG =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ && $build_sha =~ ^[0-9a-f]{40}$ ]] || {
  echo "Invalid release tag or build SHA" >&2; exit 1;
}
version=${TAG#v}
zip="dist/FrameWeave-$version-win64.zip"
[[ -f $zip && -f $zip.sha256 ]] || { echo "Missing Windows package for $TAG" >&2; exit 1; }
# ls-remote returns both the tag object and its peeled commit for an annotated tag.
refs=$(git ls-remote "https://github.com/$GITHUB_REPOSITORY.git" "refs/tags/$TAG" "refs/tags/$TAG^{}")
if [[ -n $refs ]]; then
  tag_sha=$(awk 'NR == 1 { sha=$1 } /\^\{\}$/ { sha=$1 } END { print sha }' <<< "$refs")
  [[ $tag_sha == "$build_sha" ]] || { echo "Existing tag points to a different commit" >&2; exit 1; }
fi
plugins=(dist/FrameWeave-OBS-Plugin-*-win64.zip)
[[ ${#plugins[@]} == 1 && -f ${plugins[0]} ]] || { echo "Expected exactly one OBS plugin" >&2; exit 1; }
obszip=${plugins[0]}
source_zip="dist/FrameWeave-$version-streaming-source.zip"
archives=(dist/*.zip)
[[ ${#archives[@]} == 3 ]] || { echo "Unexpected release archives" >&2; exit 1; }
for package in "$zip" "$obszip" "$source_zip"; do
  # The build wrote the checksum with Windows line endings.
  name=${package##*/}
  hash=$(sha256sum "$package" | cut -d' ' -f1)
  recorded=$(tr -d '\r' < "$package.sha256")
  [[ ${recorded,,} == "${hash}  ${name,,}" ]] || { echo "Checksum mismatch: $name" >&2; exit 1; }
done
sum=$(cat "$zip.sha256")
obssum=$(cat "$obszip.sha256")
if [ "$FULL_RELEASE" = "true" ]; then
  STATUS_LINE="Please report anything that doesn't work in Issues."
  KIND=(--latest)
else
  STATUS_LINE="**Preview build.** Please report what works and what doesn't in Issues."
  KIND=(--prerelease)
fi
# The setup guide (recommended settings, console, Windows and HDR) as a plain text file.
if [ -f "$source_dir/docs/FrameWeave-Setup-Guide.txt" ]; then cp "$source_dir/docs/FrameWeave-Setup-Guide.txt" dist/FrameWeave-Setup-Guide.txt; fi
{
  echo "FrameWeave $TAG ($version), built from commit $build_sha"
  echo "Build recipe: .github/workflows/build-windows.yml at that commit"
  echo
  echo "Pinned components:"
  if [ -f "$source_dir/.github/workflows/build-windows.yml" ]; then
    sed -n '/^env:/,/^jobs:/p' "$source_dir/.github/workflows/build-windows.yml" | grep -E '^  [A-Z0-9_]+:' | sed -E 's/^  /  /; s/ +#.*$//'
  fi
  echo "  Qt: 6.8.3 (from Veyra's runtime package, imports checked by the build)"
  echo "  Veyra runtime package: downloaded by the installer from Veyra's own release, SHA-256 pinned"
} > "dist/FrameWeave-$version-components.txt"
# One checksum list for every file, and the pinned components this build was made from (from the
# env block of build-windows.yml), so anyone can check what they downloaded and what is inside.
(cd dist && {
  files=(*.zip *-components.txt)
  if [[ -f FrameWeave-Setup-Guide.txt ]]; then files+=(FrameWeave-Setup-Guide.txt); fi
  sha256sum "${files[@]}" > SHA256SUMS.txt
})
# Signed updates: the app installs an update only when this signature over SHA256SUMS.txt verifies
# with the public key in include/veyra/Brand.h (RSA-PSS, SHA-256, 32-byte salt). The private key
# comes from the repository secret UPDATE_SIGNING_KEY and only ever exists in this job's memory.
if [ "${NOTES_ONLY:-}" = "true" ]; then
  echo "Notes only: nothing is signed and no file is uploaded"
elif [ -n "${UPDATE_SIGNING_KEY:-}" ]; then
  umask 077
  keyfile=$(mktemp -p /dev/shm 2>/dev/null || mktemp)
  printf '%s\n' "$UPDATE_SIGNING_KEY" > "$keyfile"
  openssl dgst -sha256 -sigopt rsa_padding_mode:pss -sigopt rsa_pss_saltlen:32 -sign "$keyfile" -out dist/SHA256SUMS.txt.sig dist/SHA256SUMS.txt
  openssl pkey -in "$keyfile" -pubout -out /tmp/update-public.pem
  shred -u "$keyfile" 2>/dev/null || rm -f "$keyfile"
  openssl dgst -sha256 -sigopt rsa_padding_mode:pss -sigopt rsa_pss_saltlen:32 -verify /tmp/update-public.pem -signature dist/SHA256SUMS.txt.sig dist/SHA256SUMS.txt
  echo "Signed SHA256SUMS.txt (public key fingerprint: $(openssl pkey -pubin -in /tmp/update-public.pem -outform DER | sha256sum | cut -c1-16))"
else
  echo "::warning::UPDATE_SIGNING_KEY is not set: this release is not signed and the app will only open its page"
fi
cat > notes.md <<NOTES
## Install

1. Download **$(basename "$zip")** below and extract it.
2. Double-click **Install-FrameWeave.cmd**.

The installer downloads Veyra's official runtime package (about 750 MB, NVIDIA or AMD to match your
graphics card) from Veyra's GitHub release, checks its SHA-256, and installs FrameWeave with Start menu
and desktop shortcuts. No administrator rights needed.

$STATUS_LINE

SHA-256: \`$sum\`

## Setup guide

**FrameWeave-Setup-Guide.txt** has the recommended settings to start from, how to tune them, the
settings for PS5, Xbox and Switch, the Windows settings and when to use HDR. Also on the website:
https://dosukasol.github.io/PushFPS/#guide

## OBS Studio plugin

**$(basename "$obszip")** adds a *FrameWeave* source to OBS Studio 28+ (Windows): your FrameWeave picture,
frame generation included, in any scene. Extract it and double-click **Install-FrameWeave-OBS.cmd**.

SHA-256: \`$obssum\`

## Linux and macOS (preview)

FrameWeave also runs on Linux and macOS with its own frame generation, Frame Pacing Repair and Live HUD
Lock (no vendor components). The packages (*linux-x86_64* folder or AppImage, *macos-universal* app)
are attached to this release when their build finishes. The README says what the preview includes.

## Checking the download

**SHA256SUMS.txt** lists the SHA-256 of the Windows release payloads, source archive and component list; ***-components.txt** lists the pinned components
this build was made from.

## License and source

FrameWeave is free software under the GNU GPL-3.0 and comes with no warranty. It is a modified version of
[Veyra](https://github.com/Likely7/Veyra-NRVideo) by Likely7 (see NOTICE.md). Corresponding source:
the **$TAG** tag of this repository (also attached by GitHub as *Source code*); the build recipe is
\`.github/workflows/build-windows.yml\` at that tag. The streaming libraries built into FrameWeave
(chiaki-ng, AGPL-3.0 with the OpenSSL exception; moonlight-common-c, GPL-3.0; and the vcpkg recipes of
OpenSSL, Opus, SDL3, libdatachannel and the other static libraries) are in the attached
*streaming-source* archive.
NOTES
# What's new for this tag, written in the repository (packaging/release-notes/<tag>.md).
if [ -f "packaging/release-notes/$TAG.md" ]; then
  { cat "packaging/release-notes/$TAG.md"; echo; cat notes.md; } > notes-full.md && mv notes-full.md notes.md
fi
if [ "${NOTES_ONLY:-}" = "true" ]; then
  gh release view "$TAG" -R "$GITHUB_REPOSITORY" > /dev/null
  gh release edit "$TAG" -R "$GITHUB_REPOSITORY" --notes-file notes.md
  echo "Wrote the notes of $TAG again; its files were not touched."
  exit 0
fi
# A manual run creates the tag on the commit it built; a tag push already has it.
# Run again for a tag that already has a release (a pre-release promoted to a full release
# when the owner asks): replace its files and notes and change its kind instead of failing.
if gh release view "$TAG" -R "$GITHUB_REPOSITORY" > /dev/null 2>&1; then
  if [[ -z $refs ]]; then
    draft_target=$(gh release view "$TAG" -R "$GITHUB_REPOSITORY" --json targetCommitish --jq .targetCommitish)
    [[ $draft_target == "$build_sha" ]] || { echo "Existing draft targets a different revision" >&2; exit 1; }
  fi
  gh release upload "$TAG" dist/* -R "$GITHUB_REPOSITORY" --clobber
  if [ "$FULL_RELEASE" = "true" ]; then EDIT=(--prerelease=false --latest); else EDIT=(--prerelease); fi
  gh release edit "$TAG" -R "$GITHUB_REPOSITORY" --title "FrameWeave $TAG" --notes-file notes.md "${EDIT[@]}"
elif ! gh release create "$TAG" dist/* -R "$GITHUB_REPOSITORY" --target "$build_sha" --title "FrameWeave $TAG" \
    --notes-file notes.md "${KIND[@]}"; then
  # GitHub does not let the workflow token create a tag when the commits since the last release
  # change workflow files (403 "Resource not accessible by integration"). A draft creates no tag,
  # so the files and notes are kept as a draft that the owner publishes with one click.
  echo "::warning::GitHub refused to create tag $TAG with the workflow token; saving the release as a draft"
  gh release create "$TAG" dist/* -R "$GITHUB_REPOSITORY" --target "$build_sha" --title "FrameWeave $TAG" \
    --notes-file notes.md --draft
  echo "::notice::Draft release $TAG is ready: publish it under Releases (it becomes a tag on $build_sha)."
fi
