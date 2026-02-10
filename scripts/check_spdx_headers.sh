#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025-2026 Kernel Forge LLC

set -euo pipefail

repo_root="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$repo_root"

mapfile -t candidates < <(
  git ls-files \
    '*.c' '*.h' '*.cpp' '*.hpp' \
    'CMakeLists.txt' '*.cmake' \
    '*.sh' '*.py' '*.yml' '*.yaml' \
    '*.md' '.gitignore'
)

missing=()
checked=0

for file in "${candidates[@]}"; do
  case "$file" in
    external/*|build/*|third_party/*|vendor/*)
      continue
      ;;
  esac

  case "$file" in
    src/*|tests/*|include/*|scripts/*|.github/workflows/*|CMakeLists.txt|cmake/*|README.md|THIRD_PARTY_NOTICES.md|.gitignore)
      ;;
    *)
      continue
      ;;
  esac

  checked=$((checked + 1))

  if ! head -n 12 "$file" | grep -Eq 'SPDX-License-Identifier:[[:space:]]*Apache-2\.0'; then
    missing+=("$file")
  fi
done

if ((${#missing[@]} > 0)); then
  echo "Missing SPDX-License-Identifier: Apache-2.0 in:"
  printf ' - %s\n' "${missing[@]}"
  exit 1
fi

echo "SPDX header check passed for ${checked} first-party files."
