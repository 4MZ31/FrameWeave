#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Publishes a PushFPS GitHub release from the built files in dist/ (release notes from
# packaging/release-notes/<tag>.md plus the install, OBS and licence sections below).
# Used by build-windows.yml (release job) and release-from-run.yml (files of an earlier build).
# Env: TAG (vX.Y.Z), FULL_RELEASE (true/false), GH_TOKEN, GITHUB_REPOSITORY, GITHUB_SHA.
set -e
ls -la dist
zip=$(ls dist/PushFPS-*-win64.zip | grep -v OBS-Plugin | head -1); sum=$(cat "$zip.sha256")
if [ "$FULL_RELEASE" = "true" ]; then
  STATUS_LINE="Built and tested automatically on Windows. Please report anything that doesn't work in Issues."
  KIND=(--latest)
else
  STATUS_LINE="**Preview build.** Built and start-tested automatically on Windows; not yet tested with a real capture card and GPU. Please report what works and what doesn't in Issues."
  KIND=(--prerelease)
fi
obszip=$(ls dist/PushFPS-OBS-Plugin-*.zip | head -1); obssum=$(cat "$obszip.sha256")
# One checksum list for every file, and the pinned components this build was made from (from the
# env block of build-windows.yml), so anyone can check what they downloaded and what is inside.
(cd dist && sha256sum $(ls | grep -v -E '\.sha256$|^SHA256SUMS\.txt$|-components\.txt$') > SHA256SUMS.txt)
version=$(basename "$zip" | sed -E 's/^PushFPS-(.*)-win64\.zip$/\1/')
{
  echo "PushFPS $TAG ($version), built from commit $GITHUB_SHA"
  echo "Build recipe: .github/workflows/build-windows.yml at that commit"
  echo
  echo "Pinned components:"
  if [ -f .github/workflows/build-windows.yml ]; then
    sed -n '/^env:/,/^jobs:/p' .github/workflows/build-windows.yml | grep -E '^  [A-Z0-9_]+:' | sed -E 's/^  /  /; s/ +#.*$//'
  fi
  echo "  Qt: 6.8.3 (from Veyra's runtime package, imports checked by the build)"
  echo "  Veyra runtime package: downloaded by the installer from Veyra's own release, SHA-256 pinned"
} > "dist/PushFPS-$version-components.txt"
cat > notes.md <<NOTES
## Install

1. Download **$(basename "$zip")** below and extract it.
2. Double-click **Install-PushFPS.cmd**.

The installer downloads Veyra's official runtime package (about 750 MB, NVIDIA or AMD to match your
graphics card) from Veyra's GitHub release, checks its SHA-256, and installs PushFPS with Start menu
and desktop shortcuts. No administrator rights needed.

$STATUS_LINE

SHA-256: \`$sum\`

## OBS Studio plugin

**$(basename "$obszip")** adds a *PushFPS* source to OBS Studio 28+ (Windows): your PushFPS picture,
frame generation included, in any scene. Extract it and double-click **Install-PushFPS-OBS.cmd**.

SHA-256: \`$obssum\`

## Linux and macOS (preview)

PushFPS also runs on Linux and macOS with its own frame generation, Frame Pacing Repair and Live HUD
Lock (no vendor components). The packages (*linux-x86_64* folder or AppImage, *macos-universal* app)
are attached to this release when their build finishes. See the README for what is tested.

## Checking the download

**SHA256SUMS.txt** lists the SHA-256 of every file; ***-components.txt** lists the pinned components
this build was made from.

## License and source

PushFPS is free software under the GNU GPL-3.0 and comes with no warranty. It is a modified version of
[Veyra](https://github.com/Likely7/Veyra-NRVideo) by Likely7 (see NOTICE.md). Corresponding source:
the **$TAG** tag of this repository (also attached by GitHub as *Source code*); the build recipe is
\`.github/workflows/build-windows.yml\` at that tag. The streaming libraries built into PushFPS
(chiaki-ng, AGPL-3.0 with the OpenSSL exception; moonlight-common-c, GPL-3.0; and the vcpkg recipes of
OpenSSL, Opus, SDL3, libdatachannel and the other static libraries) are in the attached
*streaming-source* archive.
NOTES
# What's new for this tag, written in the repository (packaging/release-notes/<tag>.md).
if [ -f "packaging/release-notes/$TAG.md" ]; then
  { cat "packaging/release-notes/$TAG.md"; echo; cat notes.md; } > notes-full.md && mv notes-full.md notes.md
fi
# A manual run creates the tag on the commit it built; a tag push already has it.
# Run again for a tag that already has a release (a pre-release promoted to a full release
# when the owner asks): replace its files and notes and change its kind instead of failing.
if gh release view "$TAG" -R "$GITHUB_REPOSITORY" > /dev/null 2>&1; then
  gh release upload "$TAG" dist/* -R "$GITHUB_REPOSITORY" --clobber
  if [ "$FULL_RELEASE" = "true" ]; then EDIT=(--prerelease=false --latest); else EDIT=(--prerelease); fi
  gh release edit "$TAG" -R "$GITHUB_REPOSITORY" --title "PushFPS $TAG" --notes-file notes.md "${EDIT[@]}"
elif ! gh release create "$TAG" dist/* -R "$GITHUB_REPOSITORY" --target "$GITHUB_SHA" --title "PushFPS $TAG" \
    --notes-file notes.md "${KIND[@]}"; then
  # GitHub does not let the workflow token create a tag when the commits since the last release
  # change workflow files (403 "Resource not accessible by integration"). A draft creates no tag,
  # so the files and notes are kept as a draft that the owner publishes with one click.
  echo "::warning::GitHub refused to create tag $TAG with the workflow token; saving the release as a draft"
  gh release create "$TAG" dist/* -R "$GITHUB_REPOSITORY" --target "$GITHUB_SHA" --title "PushFPS $TAG" \
    --notes-file notes.md --draft
  echo "::notice::Draft release $TAG is ready: publish it under Releases (it becomes a tag on $GITHUB_SHA)."
fi
