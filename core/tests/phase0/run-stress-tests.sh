#!/usr/bin/env bash

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${1:-${project_root}/build}"
unit_tests="${build_dir}/tests/unit_tests/unit_tests"
timeout_seconds="${MONZERO_STRESS_TIMEOUT:-1800}"

if [[ ! -x "${unit_tests}" ]]; then
  echo "Unit-test binary not found: ${unit_tests}" >&2
  echo "Build it with: cmake --build ${build_dir} --target unit_tests -j2" >&2
  exit 1
fi

export DNS_PUBLIC="${DNS_PUBLIC:-tcp://1.1.1.1}"
export TMPDIR="${TMPDIR:-${build_dir}/test-tmp}"
mkdir -p "${TMPDIR}"

echo "Running the pruning-boundary concurrency stress test (timeout: ${timeout_seconds}s)"
exec timeout --signal=INT --kill-after=30 "${timeout_seconds}" \
  "${unit_tests}" \
  --gtest_color=no \
  --gtest_filter=cryptonote_protocol_handler.race_condition
