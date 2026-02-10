// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 Kernel Forge LLC

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "manifest.h"

struct mg_context;
struct mg_connection;
struct mg_error_data;

struct HttpServerState {
	bool use_https = true;
	bool running = false;
	int port = 8443;
	char bind_address[64] = "0.0.0.0";
	char doc_root[260] = "server";
	char public_host[128] = "localhost";
	std::chrono::steady_clock::time_point started_at{};
	std::string status;
	mg_context *ctx = nullptr;
	std::string combined_pem_path;
	std::string listening_spec;
	std::vector<const char *> mg_options;
};

std::string build_device_base_url(const HttpServerState &srv,
				  const TaxonomyPath &taxonomy);
bool publish_release_manifest(ManifestState &st, const HttpServerState &srv,
			      std::string *release_path_out,
			      std::string *manifest_path_out,
			      std::string *err_out);

bool start_civetweb(const ManifestState &st, HttpServerState &srv);
void stop_civetweb(HttpServerState &srv);
void sync_server_doc_root(const ManifestState &st, HttpServerState &srv);
