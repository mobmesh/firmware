#!/usr/bin/env bash
set -euo pipefail

artifact_name=${1:?Pages artifact name required}
artifact_id=${2:?Pages artifact ID required}
run_artifacts="repos/${GITHUB_REPOSITORY}/actions/runs/${GITHUB_RUN_ID}/artifacts?per_page=100&name=${artifact_name}"

# Upload finalization can return before the run's artifact listing includes the bundle.
for attempt in {1..12}; do
  if visible_id=$(gh api "$run_artifacts" --jq '.artifacts[] | select(.expired == false) | .id') &&
     [[ $visible_id == "$artifact_id" ]]; then
    echo "Pages artifact $artifact_name ($artifact_id) is visible."
    exit 0
  fi
  if (( attempt < 12 )); then
    sleep 5
  fi
done

echo "::error::Pages artifact $artifact_name ($artifact_id) did not appear in the run's artifact listing."
exit 1
