// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 Kernel Forge LLC

#pragma once

#include <string>
#include <vector>

struct mg_connection;

void log_line(const std::string &s);
void log_clear();
const std::vector<std::string> &log_entries();
int civetweb_log_callback(const struct mg_connection *conn,
			  const char *message);
