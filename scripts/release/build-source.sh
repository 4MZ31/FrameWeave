#!/usr/bin/env bash
# Print the commit of a tested build. Allow recovering a run whose publication job alone failed.
set -euo pipefail
[[ ${RUN_ID:-} =~ ^[1-9][0-9]*$ ]] || { echo 'Invalid run ID' >&2; exit 1; }
api="repos/$GITHUB_REPOSITORY/actions/runs/$RUN_ID"
sha=$(gh api "$api" | jq -er --arg repo "$GITHUB_REPOSITORY" '
    select(.repository.full_name == $repo and .head_repository.full_name == $repo)
    | select(.path == ".github/workflows/build-windows.yml" and .status == "completed")
    | select((.event == "workflow_dispatch" or .event == "push") and
             (.conclusion == "success" or .conclusion == "failure"))
    | .head_sha | select(test("^[0-9a-f]{40}$"))') || {
    echo 'Expected a completed build-windows push or dispatch in this repository' >&2; exit 1;
}
gh api --paginate "$api/jobs?filter=latest&per_page=100" | jq -es '
    [.[].jobs[]] as $jobs
    | ["build", "obs-plugin", "obs-integration"] | all(.[]; . as $name
        | [$jobs[] | select(.name == $name) | .conclusion] == ["success"])' >/dev/null || {
    echo 'The Windows build, OBS plugin and OBS integration must all have succeeded' >&2; exit 1;
}
printf '%s\n' "$sha"
