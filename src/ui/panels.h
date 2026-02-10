// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 Kernel Forge LLC

#pragma once

#include <string>

#include "app_config.h"
#include "manifest.h"
#include "release_store.h"

namespace ui {
void draw_current_status_panel(ManifestState &st, HttpServerState &srv,
			       AppConfig &config);
void draw_configuration_modal(ManifestState &st, AppConfig &config, bool *open);
void draw_http_server_panel(const ManifestState &st, HttpServerState &srv);
void draw_log_panel();
} // namespace ui
