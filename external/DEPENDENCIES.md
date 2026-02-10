<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2025-2026 Kernel Forge LLC -->

# Vendored Dependency Pins

This repository vendors dependency source snapshots under `external/` for deterministic, offline-friendly builds.

`cJSON` is intentionally not vendored and remains a system dependency.

## Dependency Manifest

| Name | Upstream repository | Pinned ref | Date pinned | License (SPDX) | Local modifications |
| --- | --- | --- | --- | --- | --- |
| ImGui (docking branch snapshot) | https://github.com/ocornut/imgui | Commit `6327b6306459e8d8a5919baea538eb845f82b19d` | `2026-02-08` | `MIT` | none detected |
| ImGuiFileDialog | https://github.com/aiekick/ImGuiFileDialog | Commit `e76383898501dab0d0fddc23a1ccbfb46105f5cd` | `2026-02-08` | `MIT` | none detected |
| civetweb | https://github.com/civetweb/civetweb | Tag `v1.16` (commit `d7ba35bbb649209c66e582d5a0244ba988a15159`) | `2026-02-08` | `MIT` | none detected |
| inih | https://github.com/benhoyt/inih | Commit `577ae2dee1f0d9c2d11c7f10375c1715f3d6940c` | `2026-02-08` | `BSD-3-Clause` | none detected |

## Ref Selection Notes

- ImGuiFileDialog is pinned by full commit hash (`e76383898501dab0d0fddc23a1ccbfb46105f5cd`) instead of only `v0.6.8` to keep pinning strictly content-addressed and deterministic.

## License Files In-Tree

- `external/imgui/LICENSE.txt`
- `external/ImGuiFileDialog/LICENSE`
- `external/civetweb/LICENSE.md`
- `external/inih/LICENSE.txt`

`external/inih/UPSTREAM_REF` records the upstream URL, commit, and pin date for the vendored inih snapshot.

## Update Process

1. Choose the target upstream release or commit for each dependency.
2. Replace the corresponding `external/<name>/` snapshot with upstream source content.
3. Ensure the vendored directory contains no nested `.git` metadata and is tracked as normal files (not gitlinks/submodules).
4. Update this file with:
   - Upstream URL
   - Exact pinned tag/commit
   - Pin date
   - SPDX license
   - Any local modifications (or `none`)
5. Verify license files exist in-tree for each vendored dependency.
6. Reconfigure and build (`cmake -S . -B build && cmake --build build`) and run project checks/tests.
7. For inih specifically, update `external/inih/UPSTREAM_REF` with the upstream URL, commit, and pin date.
