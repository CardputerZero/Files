#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"
PYTHON_ENV_BIN=""

if [[ -x "${ROOT_DIR}/.venv/bin/python" ]] && "${ROOT_DIR}/.venv/bin/python" -m pip --version >/dev/null 2>&1; then
    PYTHON_ENV_BIN="${ROOT_DIR}/.venv/bin/python"
elif [[ -x "${ROOT_DIR}/../.venv/bin/python" ]] && "${ROOT_DIR}/../.venv/bin/python" -m pip --version >/dev/null 2>&1; then
    PYTHON_ENV_BIN="${ROOT_DIR}/../.venv/bin/python"
else
    if [[ ! -x "${ROOT_DIR}/.venv/bin/python" ]]; then
        "${PYTHON_BIN}" -m venv "${ROOT_DIR}/.venv"
    fi

    PYTHON_ENV_BIN="${ROOT_DIR}/.venv/bin/python"
fi

if ! "${PYTHON_ENV_BIN}" -m pip --version >/dev/null 2>&1; then
    echo "Project virtualenv is missing pip. Install python3-venv, remove .venv, then run ./bootstrap.sh again." >&2
    exit 1
fi

"${PYTHON_ENV_BIN}" -m pip install --upgrade pip
"${PYTHON_ENV_BIN}" -m pip install pypng lz4 Pillow

if [[ ! -d "${ROOT_DIR}/dependencies/lvgl/.git" || \
      ! -d "${ROOT_DIR}/dependencies/spdlog/.git" || \
      ! -d "${ROOT_DIR}/dependencies/smooth_ui_toolkit/.git" || \
      ! -d "${ROOT_DIR}/dependencies/miniaudio/.git" || \
      ! -d "${ROOT_DIR}/dependencies/box2d/.git" ]]; then
    "${PYTHON_ENV_BIN}" "${ROOT_DIR}/fetch_repos.py"
fi

echo "Files bootstrap complete."
echo "Build with:"
echo "  cmake -S . -B build/sdl -DFILES_USE_SDL=ON"
echo "  cmake --build build/sdl -j$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
