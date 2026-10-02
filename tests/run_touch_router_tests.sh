#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/.test-build"
mkdir -p "${OUT_DIR}"

g++ -std=c++17 -Wall -Wextra -Werror \
  -I"${ROOT_DIR}/app/src/main/cpp" \
  "${ROOT_DIR}/app/src/main/cpp/input/touch_router.cpp" \
  "${ROOT_DIR}/tests/touch_router_test.cpp" \
  -o "${OUT_DIR}/touch_router_test"

"${OUT_DIR}/touch_router_test"
