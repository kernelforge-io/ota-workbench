// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 Kernel Forge LLC

#include "manifest.h"

#include "app_config.h"
#include <cjson/cJSON.h>
#include "log.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_set>

std::string now_utc_iso8601() {
	using namespace std::chrono;
	const auto now = system_clock::now();
	const std::time_t t = system_clock::to_time_t(now);

	std::tm tm_utc{};
#if defined(_WIN32)
	gmtime_s(&tm_utc, &t);
#else
	gmtime_r(&t, &tm_utc);
#endif

	std::ostringstream oss;
	oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
	return oss.str();
}

std::string ensure_trailing_slash(std::string s) {
	if (!s.empty() && s.back() != '/')
		s.push_back('/');
	return s;
}

void apply_config_to_manifest(ManifestState &st, const AppConfig &cfg) {
	st.signer_key_path = cfg.signer_key_path;
	st.signer_cert_path = cfg.signer_cert_path;
	st.signing_algorithm = cfg.signing_algorithm;
	st.tls_key_path = cfg.tls_key_path;
	st.tls_cert_path = cfg.tls_cert_path;
	st.package_output_dir = cfg.package_output_dir.empty()
				    ? kDefaultServeRoot
				    : cfg.package_output_dir;
	if (!cfg.last_l1.empty())
		st.taxonomy.l1 = cfg.last_l1;
	if (!cfg.last_l2.empty())
		st.taxonomy.l2 = cfg.last_l2;
	if (!cfg.last_l3.empty())
		st.taxonomy.l3 = cfg.last_l3;
}

void update_app_config_from_manifest(const ManifestState &st, AppConfig &cfg) {
	cfg.signer_key_path = st.signer_key_path;
	cfg.signer_cert_path = st.signer_cert_path;
	cfg.signing_algorithm = st.signing_algorithm;
	cfg.tls_key_path = st.tls_key_path;
	cfg.tls_cert_path = st.tls_cert_path;
	cfg.package_output_dir = st.package_output_dir.empty()
				     ? kDefaultServeRoot
				     : st.package_output_dir;
	cfg.last_l1 = st.taxonomy.l1;
	cfg.last_l2 = st.taxonomy.l2;
	cfg.last_l3 = st.taxonomy.l3;
}

void reset_manifest(ManifestState &st, const AppConfig *config) {
	st.has_manifest = true;

	st.manifest_version = "9.9.9-test";
	st.created = now_utc_iso8601();
	st.release_name = "dev-test";
	st.release_version = "0.0.1-dev";
	st.file_type = "rauc_bundle";
	st.taxonomy = {};
	st.releases.clear();
	DeviceReleaseState default_release;
	default_release.device_id = "default";
	st.releases.push_back(std::move(default_release));
	st.last_saved_path.clear();
	st.last_package_path.clear();
	st.signer_key_path.clear();
	st.signer_cert_path.clear();
	st.signing_algorithm = SignAlg::Ed25519;
	st.tls_key_path.clear();
	st.tls_cert_path.clear();
	st.package_output_dir = kDefaultServeRoot;

	if (config)
		apply_config_to_manifest(st, *config);

	log_line("New manifest created.");
}

namespace {
std::string make_filename_safe(const std::string &s) {
	std::string out = s;
	for (char &c : out) {
		const bool is_alnum = (c >= 'a' && c <= 'z') ||
				      (c >= 'A' && c <= 'Z') ||
				      (c >= '0' && c <= '9');
		if (!is_alnum && c != '-' && c != '_' && c != '.')
			c = '-';
	}
	return out;
}

std::string cjson_to_string(cJSON *root, bool pretty) {
	char *printed = pretty ? cJSON_Print(root)
			       : cJSON_PrintUnformatted(root);
	if (!printed)
		return {};
	std::string out(printed);
	cJSON_free(printed);
	return out;
}

void add_string_field(cJSON *obj, const char *key, const std::string &value) {
	cJSON_AddStringToObject(obj, key, value.c_str());
}
} // namespace

bool is_valid_component(const std::string &value, std::string *err_out,
			const char *label) {
	if (value.empty()) {
		if (err_out)
			*err_out = std::string(label) + " is required.";
		return false;
	}
	if (value == "." || value == "..") {
		if (err_out)
			*err_out = std::string(label) +
				   " cannot be '.' or '..'.";
		return false;
	}
	for (unsigned char ch : value) {
		if (ch < 0x20 || ch == 0x7F) {
			if (err_out)
				*err_out =
				    std::string(label) +
				    " cannot contain control characters.";
			return false;
		}
		const bool ok = (ch >= 'a' && ch <= 'z') ||
				(ch >= 'A' && ch <= 'Z') ||
				(ch >= '0' && ch <= '9') || ch == '-' ||
				ch == '_';
		if (!ok) {
			if (err_out)
				*err_out = std::string(label) +
					   " must use letters, numbers, "
					   "dashes, or underscores.";
			return false;
		}
	}
	return true;
}

bool is_valid_taxonomy_component(const std::string &value,
				 std::string *err_out) {
	return is_valid_component(value, err_out, "Taxonomy value");
}

bool is_valid_device_id(const std::string &value, std::string *err_out) {
	return is_valid_component(value, err_out, "Device ID");
}

std::string sanitize_filename_component(const std::string &value) {
	std::string out = make_filename_safe(value);
	if (out.empty() || out == "." || out == "..")
		out = "file";
	return out;
}

std::string build_manifest_json(const ManifestState &st) {
	const std::string manifest_version = st.manifest_version.empty()
						 ? "9.9.9-test"
						 : st.manifest_version;
	const std::string created = st.created.empty() ? now_utc_iso8601()
						       : st.created;
	const std::string release_name = st.release_name.empty()
					     ? "release"
					     : st.release_name;
	const std::string release_version = st.release_version.empty()
						? "0.0.0"
						: st.release_version;
	const std::string file_type = st.file_type.empty() ? "rauc_bundle"
							   : st.file_type;

	cJSON *root = cJSON_CreateObject();
	if (!root)
		return {};

	add_string_field(root, "manifest_version", manifest_version);
	add_string_field(root, "created", created);

	cJSON *releases_json = cJSON_AddArrayToObject(root, "releases");
	if (!releases_json) {
		cJSON_Delete(root);
		return {};
	}

	std::vector<const DeviceReleaseState *> releases;
	releases.reserve(st.releases.size());
	for (const auto &rel_state : st.releases) {
		if (!rel_state.device_id.empty())
			releases.push_back(&rel_state);
	}

	for (const auto *rel_state : releases) {
		cJSON *rel_obj = cJSON_CreateObject();
		if (!rel_obj)
			continue;
		cJSON_AddItemToArray(releases_json, rel_obj);

		add_string_field(rel_obj, "device_id", rel_state->device_id);
		add_string_field(rel_obj, "release_name", release_name);
		add_string_field(rel_obj, "release_version", release_version);
		add_string_field(rel_obj, "created", created);

		cJSON *files_json = cJSON_AddArrayToObject(rel_obj, "files");
		if (!files_json)
			continue;

		for (const auto &f : rel_state->files) {
			const std::string filename = f.filename.empty()
							 ? "file"
							 : f.filename;
			const std::string safe_filename =
			    sanitize_filename_component(filename);
			const std::string rel_path = rel_state->device_id +
						     "/" + safe_filename;

			cJSON *file_obj = cJSON_CreateObject();
			if (!file_obj)
				continue;
			cJSON_AddItemToArray(files_json, file_obj);

			add_string_field(file_obj, "file_type", file_type);
			add_string_field(file_obj, "filename", safe_filename);
			cJSON_AddNumberToObject(file_obj, "size",
						static_cast<double>(f.size));
			add_string_field(file_obj, "sha256", f.sha256);
			add_string_field(file_obj, "path", rel_path);
		}
	}

	std::string out = cjson_to_string(root, true);
	cJSON_Delete(root);
	return out;
}

std::string build_manifest_json(const Manifest &manifest) {
	const std::string manifest_version = manifest.manifest_version.empty()
						 ? "9.9.9-test"
						 : manifest.manifest_version;
	const std::string created = manifest.created.empty() ? now_utc_iso8601()
							     : manifest.created;

	cJSON *root = cJSON_CreateObject();
	if (!root)
		return {};

	add_string_field(root, "manifest_version", manifest_version);
	add_string_field(root, "created", created);

	cJSON *releases_json = cJSON_AddArrayToObject(root, "releases");
	if (!releases_json) {
		cJSON_Delete(root);
		return {};
	}

	for (const auto &rel : manifest.releases) {
		const std::string release_name = rel.release_name.empty()
						     ? "release"
						     : rel.release_name;
		const std::string release_version = rel.release_version.empty()
							? "0.0.0"
							: rel.release_version;
		const std::string release_created = rel.created.empty()
							? created
							: rel.created;

		cJSON *rel_obj = cJSON_CreateObject();
		if (!rel_obj)
			continue;
		cJSON_AddItemToArray(releases_json, rel_obj);

		add_string_field(rel_obj, "device_id", rel.device_id);
		add_string_field(rel_obj, "release_name", release_name);
		add_string_field(rel_obj, "release_version", release_version);
		add_string_field(rel_obj, "created", release_created);

		cJSON *files_json = cJSON_AddArrayToObject(rel_obj, "files");
		if (!files_json)
			continue;

		for (const auto &f : rel.files) {
			const std::string file_type = f.file_type.empty()
							  ? "rauc_bundle"
							  : f.file_type;
			const std::string filename = f.filename.empty()
							 ? "file"
							 : f.filename;
			const std::string rel_path = f.path.empty()
							 ? (rel.device_id +
							    "/" + filename)
							 : f.path;

			cJSON *file_obj = cJSON_CreateObject();
			if (!file_obj)
				continue;
			cJSON_AddItemToArray(files_json, file_obj);

			add_string_field(file_obj, "file_type", file_type);
			add_string_field(file_obj, "filename", filename);
			cJSON_AddNumberToObject(file_obj, "size",
						static_cast<double>(f.size));
			add_string_field(file_obj, "sha256", f.sha256);
			add_string_field(file_obj, "path", rel_path);
		}
	}

	std::string out = cjson_to_string(root, true);
	cJSON_Delete(root);
	return out;
}

std::string build_manifest_preview_json(const ManifestState &st,
					const Manifest *existing_manifest) {
	if (!existing_manifest)
		return build_manifest_json(st);

	const std::string release_created = st.created.empty()
						? now_utc_iso8601()
						: st.created;
	const std::string file_type = st.file_type.empty() ? "rauc_bundle"
							   : st.file_type;
	const std::string release_name = st.release_name.empty()
					     ? "release"
					     : st.release_name;
	const std::string release_version = st.release_version.empty()
						? "0.0.0"
						: st.release_version;

	Manifest merged = *existing_manifest;
	merged.manifest_version = st.manifest_version.empty()
				      ? (merged.manifest_version.empty()
					     ? "9.9.9-test"
					     : merged.manifest_version)
				      : st.manifest_version;
	if (merged.created.empty())
		merged.created = release_created;

	for (const auto &rel : st.releases) {
		if (rel.device_id.empty())
			continue;

		TargetRelease release;
		release.device_id = rel.device_id;
		release.release_name = release_name;
		release.release_version = release_version;
		release.created = release_created;
		release.files.reserve(rel.files.size());

		for (const auto &f : rel.files) {
			FileEntry entry;
			entry.file_type = file_type;
			const std::string filename = f.filename.empty()
							 ? "file"
							 : f.filename;
			const std::string safe_filename =
			    sanitize_filename_component(filename);
			entry.filename = safe_filename;
			entry.size = f.size;
			entry.sha256 = f.sha256;
			entry.path = rel.device_id + "/" + safe_filename;
			release.files.push_back(std::move(entry));
		}

		upsert_release(merged, release.device_id, release);
	}

	return build_manifest_json(merged);
}

std::string build_manifest_json_with_base(const ManifestState &st,
					  const std::string &) {
	return build_manifest_json(st);
}

std::filesystem::path get_serve_root_path(const std::string &configured) {
	namespace fs = std::filesystem;
	return configured.empty() ? fs::path(kDefaultServeRoot)
				  : fs::path(configured);
}

bool read_file_to_string(const std::filesystem::path &path, std::string *out) {
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return false;
	std::ostringstream ss;
	ss << in.rdbuf();
	if (in.bad())
		return false;
	*out = ss.str();
	return true;
}

bool parse_manifest_json(const std::string &json, Manifest *out,
			 std::string *err_out) {
	if (!out)
		return false;

	cJSON *root = cJSON_Parse(json.c_str());
	if (!root) {
		if (err_out) {
			const char *ptr = cJSON_GetErrorPtr();
			*err_out = ptr ? std::string(
					     "Invalid manifest JSON near: ") +
					     ptr
				       : "Invalid manifest JSON.";
		}
		return false;
	}

	if (!cJSON_IsObject(root)) {
		if (err_out)
			*err_out = "Manifest JSON root must be an object.";
		cJSON_Delete(root);
		return false;
	}

	auto set_error = [&](const std::string &message) {
		if (err_out)
			*err_out = message;
	};

	Manifest result;

	cJSON *manifest_version =
	    cJSON_GetObjectItemCaseSensitive(root, "manifest_version");
	if (manifest_version) {
		if (!cJSON_IsString(manifest_version) ||
		    !manifest_version->valuestring) {
			set_error("Invalid manifest_version.");
			cJSON_Delete(root);
			return false;
		}
		result.manifest_version = manifest_version->valuestring;
	}

	cJSON *created = cJSON_GetObjectItemCaseSensitive(root, "created");
	if (created) {
		if (!cJSON_IsString(created) || !created->valuestring) {
			set_error("Invalid created value.");
			cJSON_Delete(root);
			return false;
		}
		result.created = created->valuestring;
	}

	cJSON *releases = cJSON_GetObjectItemCaseSensitive(root, "releases");
	if (releases) {
		if (!cJSON_IsArray(releases)) {
			set_error(
			    "Releases must be an array in manifest JSON.");
			cJSON_Delete(root);
			return false;
		}

		cJSON *release_item = nullptr;
		cJSON_ArrayForEach(release_item, releases) {
			if (!cJSON_IsObject(release_item)) {
				set_error(
				    "Invalid release entry in manifest JSON.");
				cJSON_Delete(root);
				return false;
			}

			TargetRelease release;

			cJSON *device_id = cJSON_GetObjectItemCaseSensitive(
			    release_item, "device_id");
			if (!cJSON_IsString(device_id) ||
			    !device_id->valuestring ||
			    !device_id->valuestring[0]) {
				set_error("Release entry missing device_id in "
					  "manifest JSON.");
				cJSON_Delete(root);
				return false;
			}
			release.device_id = device_id->valuestring;

			cJSON *release_name = cJSON_GetObjectItemCaseSensitive(
			    release_item, "release_name");
			if (release_name) {
				if (!cJSON_IsString(release_name) ||
				    !release_name->valuestring) {
					set_error("Invalid release_name in "
						  "manifest JSON.");
					cJSON_Delete(root);
					return false;
				}
				release.release_name =
				    release_name->valuestring;
			}

			cJSON *release_version =
			    cJSON_GetObjectItemCaseSensitive(release_item,
							     "release_version");
			if (release_version) {
				if (!cJSON_IsString(release_version) ||
				    !release_version->valuestring) {
					set_error("Invalid release_version in "
						  "manifest JSON.");
					cJSON_Delete(root);
					return false;
				}
				release.release_version =
				    release_version->valuestring;
			}

			cJSON *release_created =
			    cJSON_GetObjectItemCaseSensitive(release_item,
							     "created");
			if (release_created) {
				if (!cJSON_IsString(release_created) ||
				    !release_created->valuestring) {
					set_error("Invalid created in manifest "
						  "JSON.");
					cJSON_Delete(root);
					return false;
				}
				release.created = release_created->valuestring;
			}

			cJSON *files = cJSON_GetObjectItemCaseSensitive(
			    release_item, "files");
			if (files) {
				if (!cJSON_IsArray(files)) {
					set_error("Files must be an array in "
						  "manifest JSON.");
					cJSON_Delete(root);
					return false;
				}

				cJSON *file_item = nullptr;
				cJSON_ArrayForEach(file_item, files) {
					if (!cJSON_IsObject(file_item)) {
						set_error("Invalid file entry "
							  "in manifest JSON.");
						cJSON_Delete(root);
						return false;
					}

					FileEntry entry;

					cJSON *file_type =
					    cJSON_GetObjectItemCaseSensitive(
						file_item, "file_type");
					if (file_type) {
						if (!cJSON_IsString(
							file_type) ||
						    !file_type->valuestring) {
							set_error(
							    "Invalid file_type "
							    "in manifest "
							    "JSON.");
							cJSON_Delete(root);
							return false;
						}
						entry.file_type =
						    file_type->valuestring;
					}

					cJSON *filename =
					    cJSON_GetObjectItemCaseSensitive(
						file_item, "filename");
					if (filename) {
						if (!cJSON_IsString(filename) ||
						    !filename->valuestring) {
							set_error(
							    "Invalid filename "
							    "in manifest "
							    "JSON.");
							cJSON_Delete(root);
							return false;
						}
						entry.filename =
						    filename->valuestring;
					}

					cJSON *size =
					    cJSON_GetObjectItemCaseSensitive(
						file_item, "size");
					if (size) {
						if (!cJSON_IsNumber(size) ||
						    size->valuedouble < 0.0) {
							set_error(
							    "Invalid size in "
							    "manifest JSON.");
							cJSON_Delete(root);
							return false;
						}
						entry.size =
						    static_cast<uint64_t>(
							size->valuedouble);
					}

					cJSON *sha256 =
					    cJSON_GetObjectItemCaseSensitive(
						file_item, "sha256");
					if (sha256) {
						if (!cJSON_IsString(sha256) ||
						    !sha256->valuestring) {
							set_error(
							    "Invalid sha256 in "
							    "manifest JSON.");
							cJSON_Delete(root);
							return false;
						}
						entry.sha256 =
						    sha256->valuestring;
					}

					cJSON *path =
					    cJSON_GetObjectItemCaseSensitive(
						file_item, "path");
					if (path) {
						if (!cJSON_IsString(path) ||
						    !path->valuestring) {
							set_error(
							    "Invalid path in "
							    "manifest JSON.");
							cJSON_Delete(root);
							return false;
						}
						entry.path = path->valuestring;
					}

					release.files.push_back(
					    std::move(entry));
				}
			}

			result.releases.push_back(std::move(release));
		}
	}

	cJSON_Delete(root);
	*out = std::move(result);
	return true;
}

bool load_manifest_file(const std::filesystem::path &path, Manifest *out,
			std::string *err_out) {
	std::string json;
	if (!read_file_to_string(path, &json)) {
		if (err_out)
			*err_out = "Failed to read manifest: " + path.string();
		return false;
	}
	return parse_manifest_json(json, out, err_out);
}

bool manifest_has_device(const Manifest &manifest,
			 const std::string &device_id) {
	return find_release_index_by_device_id(manifest, device_id) >= 0;
}

int find_release_index_by_device_id(const Manifest &manifest,
				    const std::string &device_id) {
	for (size_t i = 0; i < manifest.releases.size(); ++i) {
		if (manifest.releases[i].device_id == device_id)
			return static_cast<int>(i);
	}
	return -1;
}

void upsert_release(Manifest &manifest, const std::string &device_id,
		    const TargetRelease &release) {
	TargetRelease updated = release;
	updated.device_id = device_id;
	const int idx = find_release_index_by_device_id(manifest, device_id);
	if (idx >= 0)
		manifest.releases[static_cast<size_t>(idx)] = std::move(
		    updated);
	else
		manifest.releases.push_back(std::move(updated));
}

bool manifest_has_basic_info(const ManifestState &st) {
	if (st.taxonomy.l1.empty() || st.taxonomy.l2.empty() ||
	    st.taxonomy.l3.empty())
		return false;
	if (!is_valid_taxonomy_component(st.taxonomy.l1, nullptr))
		return false;
	if (!is_valid_taxonomy_component(st.taxonomy.l2, nullptr))
		return false;
	if (!is_valid_taxonomy_component(st.taxonomy.l3, nullptr))
		return false;
	return !st.release_name.empty() && !st.release_version.empty();
}

bool manifest_ready_for_release(const ManifestState &st,
				bool existing_has_default) {
	if (!manifest_has_basic_info(st))
		return false;
	if (st.releases.empty())
		return false;
	bool has_default = false;
	std::unordered_set<std::string> device_ids;
	for (const auto &rel : st.releases) {
		if (!is_valid_device_id(rel.device_id, nullptr))
			return false;
		if (!device_ids.insert(rel.device_id).second)
			return false;
		if (rel.files.empty())
			return false;
		if (rel.device_id == "default")
			has_default = true;
	}
	return has_default || existing_has_default;
}

uint64_t manifest_total_size(const ManifestState &st) {
	uint64_t total = 0;
	for (const auto &rel : st.releases)
		for (const auto &f : rel.files)
			total += f.size;
	return total;
}

void add_bundle_file(DeviceReleaseState &rel, const std::string &local_path) {
	try {
		std::filesystem::path p(local_path);
		if (!std::filesystem::exists(p)) {
			log_line("File does not exist: " + local_path);
			return;
		}

		StagedFile mf;
		mf.local_path = local_path;
		mf.filename = sanitize_filename_component(
		    p.filename().string());
		mf.size = static_cast<uint64_t>(std::filesystem::file_size(p));
		mf.sha256 = "<pending>";

		rel.files.push_back(std::move(mf));
		log_line("Added file: " + local_path);
	} catch (const std::exception &e) {
		log_line(std::string("Failed to add file: ") + e.what());
	}
}
