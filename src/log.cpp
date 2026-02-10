// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 Kernel Forge LLC

#include "log.h"

#include "civetweb.h"

#include <algorithm>

namespace {
constexpr size_t kMaxLogLines = 500;
constexpr size_t kTrimmedLines = 100;
std::vector<std::string> g_log_lines;
} // namespace

void log_line(const std::string &s) {
	g_log_lines.push_back(s);
	if (g_log_lines.size() > kMaxLogLines) {
		const size_t erase_count = std::min(kTrimmedLines,
						    g_log_lines.size());
		g_log_lines.erase(g_log_lines.begin(),
				  g_log_lines.begin() +
				      static_cast<std::ptrdiff_t>(erase_count));
	}
}

void log_clear() { g_log_lines.clear(); }

const std::vector<std::string> &log_entries() { return g_log_lines; }

int civetweb_log_callback(const struct mg_connection *, const char *message) {
	if (message && *message)
		log_line(std::string("[CivetWeb] ") + message);
	return 0;
}
