// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 Kernel Forge LLC

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "crypto.h"

struct AppConfig;

constexpr const char kDefaultServeRoot[] = "server";

struct TaxonomyPath {
	std::string l1;
	std::string l2;
	std::string l3;
};

struct FileEntry {
	std::string file_type;
	std::string filename;
	uint64_t size = 0;
	std::string sha256;
	std::string path;
};

struct TargetRelease {
	std::string device_id;
	std::string release_name;
	std::string release_version;
	std::string created;
	std::vector<FileEntry> files;
};

struct Manifest {
	std::string manifest_version;
	std::string created;
	std::vector<TargetRelease> releases;
};

struct StagedFile {
	std::string local_path;
	std::string filename;
	uint64_t size = 0;
	std::string sha256;
};

struct DeviceReleaseState {
	std::string device_id;
	std::vector<StagedFile> files;
};

struct ManifestState {
	bool has_manifest = false;

	std::string manifest_version;
	std::string created;
	std::string release_name;
	std::string release_version;
	std::string file_type;
	TaxonomyPath taxonomy;
	std::vector<DeviceReleaseState> releases;
	std::string last_saved_path;
	std::string last_package_path;

	std::string signer_key_path;
	std::string signer_cert_path;
	SignAlg signing_algorithm = SignAlg::Ed25519;
	std::string tls_key_path;
	std::string tls_cert_path;
	std::string package_output_dir = kDefaultServeRoot;
};

std::string now_utc_iso8601();
std::string ensure_trailing_slash(std::string s);

void reset_manifest(ManifestState &st, const AppConfig *config = nullptr);
void apply_config_to_manifest(ManifestState &st, const AppConfig &cfg);
void update_app_config_from_manifest(const ManifestState &st, AppConfig &cfg);

std::string build_manifest_json(const ManifestState &st);
std::string build_manifest_json(const Manifest &manifest);
std::string build_manifest_preview_json(const ManifestState &st,
					const Manifest *existing_manifest);
std::string build_manifest_json_with_base(const ManifestState &st,
					  const std::string &base_override);

std::filesystem::path get_serve_root_path(const std::string &configured);

bool read_file_to_string(const std::filesystem::path &path, std::string *out);

bool is_valid_taxonomy_component(const std::string &value,
				 std::string *err_out);
bool is_valid_device_id(const std::string &value, std::string *err_out);
std::string sanitize_filename_component(const std::string &value);

bool parse_manifest_json(const std::string &json, Manifest *out,
			 std::string *err_out);
bool load_manifest_file(const std::filesystem::path &path, Manifest *out,
			std::string *err_out);
bool manifest_has_device(const Manifest &manifest,
			 const std::string &device_id);
int find_release_index_by_device_id(const Manifest &manifest,
				    const std::string &device_id);
void upsert_release(Manifest &manifest, const std::string &device_id,
		    const TargetRelease &release);

bool manifest_has_basic_info(const ManifestState &st);
bool manifest_ready_for_release(const ManifestState &st,
				bool existing_has_default = false);
uint64_t manifest_total_size(const ManifestState &st);
void add_bundle_file(DeviceReleaseState &rel, const std::string &local_path);
