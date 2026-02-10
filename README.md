<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright (c) 2025-2026 Kernel Forge LLC -->

# OTA Workbench

`ota-workbench` is a desktop tool for building OTA manifest metadata, signing release artifacts, and serving update payloads over an embedded HTTP(S) endpoint for integration and lab workflows. The UI uses ImGui/GLFW, while manifest/signing/server logic is implemented in native C++ for deterministic local builds and CI.

## Dependencies

Vendored dependency pins live in `external/DEPENDENCIES.md`.  
`cJSON` is intentionally a system dependency and is not vendored.

### Ubuntu (apt)

```bash
sudo apt-get update
sudo apt-get install -y \
  cmake \
  ninja-build \
  pkg-config \
  libglfw3-dev \
  libssl-dev \
  libx11-dev \
  libxext-dev \
  libgl1-mesa-dev \
  libcjson-dev
```

### Fedora (dnf)

```bash
sudo dnf install -y \
  cmake \
  ninja-build \
  pkgconf-pkg-config \
  glfw-devel \
  openssl-devel \
  libX11-devel \
  libXext-devel \
  mesa-libGL-devel \
  cjson-devel
```

## Build

```bash
cmake -S . -B build -G Ninja \
  -DOTA_WORKBENCH_STATIC_RUNTIME=OFF \
  -DOTA_WORKBENCH_WERROR=ON \
  -DOTA_WORKBENCH_BUILD_TESTS=ON
cmake --build build --parallel
```

Run the application:

```bash
./build/ota-workbench
```

## Test

```bash
ctest --test-dir build --output-on-failure --no-tests=error
```

## Run CI Locally With act

Create `~/.config/act/actrc` (example):

```text
--container-architecture linux/amd64
-P ubuntu-24.04=ghcr.io/catthehacker/ubuntu:act-24.04
```

Run the CI workflow locally:

```bash
act -W .github/workflows/ci.yml -j build-and-test -v
```

## License

This project is licensed under Apache-2.0. See `LICENSE`.
