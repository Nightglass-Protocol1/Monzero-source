#!/usr/bin/env bash

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${1:-${project_root}/build}"
core_tests="${build_dir}/tests/core_tests/core_tests"
timeout_seconds="${MONZERO_ADVERSARIAL_TIMEOUT:-1800}"
filter='^(txpool_spend_key_(public|all)|txpool_double_spend_(norelay|local|keyimage)|gen_double_spend_.*)$'

if [[ ! -x "${core_tests}" ]]; then
  echo "Core-test binary not found: ${core_tests}" >&2
  echo "Build it with: cmake --build ${build_dir} --target core_tests -j2" >&2
  exit 1
fi

export TMPDIR="${TMPDIR:-${build_dir}/test-tmp}"
mkdir -p "${TMPDIR}"

echo "Running Monzero double-spend and key-image adversarial gate (timeout: ${timeout_seconds}s)"
exec timeout --signal=INT --kill-after=30 "${timeout_seconds}" \
  "${core_tests}" \
  --generate_and_play_test_data \
  --filter "${filter}"
