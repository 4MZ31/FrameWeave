#!/usr/bin/env bash
# Offline regression tests: gh is a test double and the tag remote is a disposable local repo.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"
mkdir -p bin dist
cat > bin/gh <<'MOCK'
#!/usr/bin/env bash
case "$*" in
  *'/jobs?'*) cat jobs.json ;;
  'api '*) cat run.json ;;
  'release view '*) exit 1 ;;
  'release create '*) printf '%s\n' "$@" > published ;;
  *) exit 1 ;;
esac
MOCK
chmod +x bin/gh
export PATH="$tmp/bin:$PATH" RUN_ID=42 GITHUB_REPOSITORY=local/FrameWeave FULL_RELEASE=true TAG=v1.0.0
unset UPDATE_SIGNING_KEY NOTES_ONLY
git init -q --bare mirror
git init -q source
git -C source -c user.name=test -c user.email=test@example.invalid commit -q --allow-empty -m build
BUILD_SHA=$(git -C source rev-parse HEAD)
export BUILD_SHA
git -C source -c user.name=test -c user.email=test@example.invalid commit -q --allow-empty -m dispatch
GITHUB_SHA=$(git -C source rev-parse HEAD)
export GITHUB_SHA
git -C source remote add origin "$tmp/mirror"
git -C source push -q origin HEAD:main
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0="url.file://$tmp/mirror.insteadOf" GIT_CONFIG_VALUE_0="https://github.com/$GITHUB_REPOSITORY.git"

cat > run.json <<JSON
{"repository":{"full_name":"$GITHUB_REPOSITORY"},"head_repository":{"full_name":"$GITHUB_REPOSITORY"},"path":".github/workflows/build-windows.yml","status":"completed","event":"workflow_dispatch","conclusion":"failure","head_sha":"$BUILD_SHA"}
JSON
echo '{"jobs":[{"name":"build","conclusion":"success"},{"name":"obs-plugin","conclusion":"success"},{"name":"obs-integration","conclusion":"success"}]}' > jobs.json
[[ $(bash "$root/scripts/release/build-source.sh") == "$BUILD_SHA" ]]
reject() {
  if "$@" >/dev/null 2>&1; then echo "Unexpected success: $*" >&2; exit 1; fi
  [[ ! -e published ]]
}
sed -i 's/workflow_dispatch/pull_request/' run.json
reject bash "$root/scripts/release/build-source.sh"
sed -i 's/pull_request/push/' run.json
sed -i 's/"obs-integration","conclusion":"success"/"obs-integration","conclusion":"failure"/' jobs.json
reject bash "$root/scripts/release/build-source.sh"

for name in FrameWeave-1.0.0-win64.zip FrameWeave-1.0.0-streaming-source.zip FrameWeave-OBS-Plugin-1.2.0-win64.zip; do
  echo "$name" > "dist/$name"
  (cd dist && sha256sum "$name" | sed 's/$/\r/' > "$name.sha256")
done
publish() { bash "$root/scripts/release/publish.sh"; }
TAG=v2.0.0 reject publish
git -C source tag "$TAG" "$GITHUB_SHA"
git -C source push -q origin "refs/tags/$TAG"
reject publish
git -C source -c user.name=test -c user.email=test@example.invalid tag -fa "$TAG" "$BUILD_SHA" -m release
git -C source push -q --force origin "refs/tags/$TAG"
echo changed >> dist/FrameWeave-1.0.0-win64.zip
reject publish
echo FrameWeave-1.0.0-win64.zip > dist/FrameWeave-1.0.0-win64.zip
publish
grep -Fxq -- "$BUILD_SHA" published
grep -Fq -- "$BUILD_SHA" dist/FrameWeave-1.0.0-components.txt
(cd dist && sha256sum -c SHA256SUMS.txt)
echo 'Release provenance checks passed'
