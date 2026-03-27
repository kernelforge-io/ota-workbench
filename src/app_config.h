// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 Kernel Forge LLC

#pragma once

#include <filesystem>
#include <string>

#include "crypto.h"

struct ManifestState;

struct UiTaxonomyLabels {
	std::string l1_label = "Fleet Group";
	std::string l2_label = "Sub Group";
	std::string l3_label = "Function";
	std::string device_selector_label = "Device ID";
};

struct AppConfig {
	std::string signer_key_path;
	std::string signer_cert_path;
	SignAlg signing_algorithm = SignAlg::Ed25519;
	std::string tls_key_path;
	std::string tls_cert_path;
	std::string package_output_dir = "server";
	std::string last_l1;
	std::string last_l2;
	std::string last_l3;
	int last_tab_index = 0;
	UiTaxonomyLabels taxonomy_labels;
};

std::filesystem::path get_config_directory();
std::filesystem::path get_config_file_path();
bool load_app_config(AppConfig &cfg);
bool save_app_config(const AppConfig &cfg);
void persist_configuration(const ManifestState &st, AppConfig &cfg);
