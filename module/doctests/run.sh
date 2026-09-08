#!/usr/bin/env bash
#
# Execute the netgraph M0 doc-test end-to-end and regenerate its Markdown.
#
# One spec:
#   netgraph-module-m0.test.yaml — packages THIS module as an .lgx, installs it
#     with lgpm, drives it through a headless logoscore daemon, opens a known
#     loopback connection, enables collection scoped to that connection's PID,
#     and asserts the endpoint shows up in snapshot().
#
# The runner is the shared `doctest` CLI
# (https://github.com/logos-co/logos-doctest), invoked directly via its flake.
# `doctest run` executes every command in a temp directory and asserts on the
# output; `doctest generate` renders the same spec to Markdown under outputs/;
# `doctest clean` strips build artifacts so only the generated docs remain.
#
# To run against a local logos-doctest checkout instead of the published flake,
# set DOCTEST, e.g.:  DOCTEST="nix run path:../../logos-doctest --" ./run.sh
#
set -euo pipefail

# Run from this doctests/ directory regardless of where the script is invoked from.
cd "$(dirname "$0")"

# The doctest CLI. Override by exporting DOCTEST (space-separated command).
read -r -a DOCTEST <<< "${DOCTEST:-nix run github:logos-co/logos-doctest --}"
OUTPUT_DIR="./outputs"

# Build the doc-test against THIS repo's current commit rather than the latest
# published flake. The spec pins `github:corpetty/netgraph-module{release}` to
# $COMMIT via --release-for, so it packages exactly what is checked out here.
# Override by exporting COMMIT (e.g. a tag), or set COMMIT="" to fall back to
# latest master.
#
# Note: nix fetches the commit from the GitHub remote, so $COMMIT must be pushed
# to corpetty/netgraph-module. A local-only / uncommitted HEAD won't resolve;
# export COMMIT="" (or push first) in that case. The doctest also can't run until
# the repo is published, since it fetches the module's #lgx over the network.
#
# The doctest lives under module/, but git commits are repo-wide, so HEAD here is
# the repo commit and the spec's `?dir=module` selects the module flake within it.
COMMIT="${COMMIT-$(git rev-parse HEAD)}"
RELEASE_FOR=()
if [ -n "${COMMIT}" ]; then
  RELEASE_FOR=(--release-for "netgraph-module=${COMMIT}")
  echo "==> Pinning netgraph-module to ${COMMIT}"
else
  echo "==> COMMIT empty; building from latest netgraph-module master"
fi

echo "==> Clearing previous ${OUTPUT_DIR}/"
# A prior run copies module artifacts out of the read-only nix store, so the
# directories land read-only (r-x) too. `rm -rf` can't delete files inside a
# directory it can't write to, so restore write permission first.
if [ -e "${OUTPUT_DIR}" ]; then
  chmod -R u+w "${OUTPUT_DIR}" 2>/dev/null || true
fi
rm -rf "${OUTPUT_DIR}"
mkdir -p "${OUTPUT_DIR}"

# Read the `output:` filename a spec renders to (e.g. "foo.md") from its YAML.
spec_output() {
  sed -n 's/^output:[[:space:]]*//p' "$1" | head -n1 | tr -d '"'
}

for SPEC in *.test.yaml; do
  STEM="${SPEC%.test.yaml}"
  MD="$(spec_output "${SPEC}")"
  : "${MD:=${STEM}.md}"

  echo "==> Running ${SPEC} into ${OUTPUT_DIR}/"
  # ${RELEASE_FOR[@]+...} guards the expansion so an empty array doesn't trip
  # `set -u` on older bash (e.g. macOS's stock 3.2).
  "${DOCTEST[@]}" run "${SPEC}" \
    --verbose \
    --continue-on-fail \
    ${RELEASE_FOR[@]+"${RELEASE_FOR[@]}"} \
    --output-dir "${OUTPUT_DIR}/"

  echo "==> Generating ${OUTPUT_DIR}/${MD}"
  "${DOCTEST[@]}" generate "${SPEC}" \
    ${RELEASE_FOR[@]+"${RELEASE_FOR[@]}"} \
    -o "${OUTPUT_DIR}/${MD}"
done

echo "==> Cleaning build artifacts from ${OUTPUT_DIR}/ (keeps .md)"
"${DOCTEST[@]}" clean "${OUTPUT_DIR}" --verbose

echo "==> Done. Rendered docs are in ${OUTPUT_DIR}/"
