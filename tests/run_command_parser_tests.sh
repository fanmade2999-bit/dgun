#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/.test-build"
mkdir -p "${OUT_DIR}"

g++ -std=c++17 -Wall -Wextra -Werror \
  -I"${ROOT_DIR}/app/src/main/cpp" \
  "${ROOT_DIR}/app/src/main/cpp/console/command_parser.cpp" \
  "${ROOT_DIR}/app/src/main/cpp/console/command_console.cpp" \
  "${ROOT_DIR}/tests/command_parser_test.cpp" \
  -o "${OUT_DIR}/command_parser_test"

"${OUT_DIR}/command_parser_test"
