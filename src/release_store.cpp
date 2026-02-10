// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 Kernel Forge LLC

#include "release_store.h"

#include "crypto.h"
#include "log.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <vector>

#include "civetweb.h"

namespace {
std::string describe_civetweb_error(const mg_error_data &err) {
	if (err.code == MG_ERROR_DATA_CODE_OK &&
	    (!err.text || err.text[0] == '\0'))
		return "Unknown error";

	std::ostringstream oss;
	oss << "code " << err.code;
	if (err.code_sub != 0)
		oss << "." << err.code_sub;
	if (err.text && err.text[0] != '\0')
		oss << " - " << err.text;
	return oss.str();
}

bool save_text_file(const std::string &path, const std::string &content,
		    std::string *err_out) {
	namespace fs = std::filesystem;
	const fs::path target(path);
	const fs::path dir = target.parent_path();
	const std::string filename = target.filename().string();
	const auto stamp =
	    std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path temp_path = dir /
				   (filename + ".tmp." + std::to_string(stamp));

	std::ofstream f(temp_path, std::ios::binary | std::ios::trunc);
	if (!f) {
		if (err_out)
			*err_out = "Failed to open for write: " +
				   temp_path.string();
		return false;
	}
	f.write(content.data(), static_cast<std::streamsize>(content.size()));
	if (!f) {
		std::error_code cleanup_ec;
		fs::remove(temp_path, cleanup_ec);
		if (err_out)
			*err_out = "Failed to write file: " +
				   temp_path.string();
		return false;
	}
	f.close();

	std::error_code ec;
	fs::rename(temp_path, target, ec);
	if (ec) {
		std::error_code rm_ec;
		fs::remove(target, rm_ec);
		fs::rename(temp_path, target, ec);
	}
	if (ec) {
		std::error_code cleanup_ec;
		fs::remove(temp_path, cleanup_ec);
		if (err_out)
			*err_out = "Failed to replace file: " + path + " (" +
				   ec.message() + ")";
		return false;
	}
	return true;
}

bool clear_directory_contents(const std::filesystem::path &dir_path,
			      std::string *err_out) {
	namespace fs = std::filesystem;
	std::error_code ec;
	if (!fs::exists(dir_path, ec))
		return true;
	if (ec) {
		if (err_out)
			*err_out = "Failed to read directory: " +
				   dir_path.string();
		return false;
	}
	if (!fs::is_directory(dir_path, ec)) {
		if (err_out)
			*err_out = "Path is not a directory: " +
				   dir_path.string();
		return false;
	}
	std::vector<fs::path> entries;
	for (fs::directory_iterator it(dir_path, ec);
	     it != fs::directory_iterator(); it.increment(ec)) {
		if (ec)
			break;
		entries.push_back(it->path());
	}
	if (ec) {
		if (err_out)
			*err_out = "Failed to read directory: " +
				   dir_path.string();
		return false;
	}
	for (const auto &entry : entries) {
		fs::remove_all(entry, ec);
		if (ec) {
			if (err_out)
				*err_out = "Failed to remove: " +
					   entry.string();
			return false;
		}
	}
	return true;
}

std::string build_listening_spec(const HttpServerState &srv) {
	std::ostringstream oss;
	if (srv.bind_address[0] != '\0')
		oss << srv.bind_address << ":";
	oss << srv.port;
	if (srv.use_https)
		oss << "s";
	return oss.str();
}

bool validate_component(const std::string &value,
			bool (*validator)(const std::string &, std::string *),
			const char *label, std::string *err_out) {
	std::string err;
	if (!validator(value, &err)) {
		if (err_out)
			*err_out = std::string(label) + ": " + err;
		return false;
	}
	return true;
}
} // namespace

std::string build_device_base_url(const HttpServerState &srv,
				  const TaxonomyPath &taxonomy) {
	const std::string scheme = srv.use_https ? "https" : "http";
	const std::string host = srv.public_host[0] ? srv.public_host
						    : "localhost";
	std::ostringstream oss;
	oss << scheme << "://" << host << ":" << srv.port << "/" << taxonomy.l1
	    << "/" << taxonomy.l2 << "/" << taxonomy.l3 << "/";
	return oss.str();
}

bool publish_release_manifest(ManifestState &st, const HttpServerState &srv,
			      std::string *release_path_out,
			      std::string *manifest_path_out,
			      std::string *err_out) {
	namespace fs = std::filesystem;

	if (!validate_component(st.taxonomy.l1, is_valid_taxonomy_component,
				"L1", err_out))
		return false;
	if (!validate_component(st.taxonomy.l2, is_valid_taxonomy_component,
				"L2", err_out))
		return false;
	if (!validate_component(st.taxonomy.l3, is_valid_taxonomy_component,
				"L3", err_out))
		return false;
	if (st.release_name.empty()) {
		if (err_out)
			*err_out = "Release name is required.";
		return false;
	}
	if (st.release_version.empty()) {
		if (err_out)
			*err_out = "Release version is required.";
		return false;
	}
	if (st.releases.empty()) {
		if (err_out)
			*err_out = "At least one device entry is required.";
		return false;
	}

	std::unordered_set<std::string> device_ids;
	bool has_default = false;
	for (const auto &rel : st.releases) {
		if (!validate_component(rel.device_id, is_valid_device_id,
					"Device ID", err_out))
			return false;
		if (!device_ids.insert(rel.device_id).second) {
			if (err_out)
				*err_out = "Duplicate device_id: " +
					   rel.device_id;
			return false;
		}
		if (rel.device_id == "default")
			has_default = true;
		if (rel.files.empty()) {
			if (err_out)
				*err_out = "Each device entry must include at "
					   "least one file.";
			return false;
		}
	}

	if (st.signer_key_path.empty()) {
		if (err_out)
			*err_out = "Signer key is required to publish.";
		return false;
	}
	if (st.signer_cert_path.empty()) {
		if (err_out)
			*err_out = "Signer certificate is required to publish.";
		return false;
	}

	if (!fs::exists(st.signer_key_path)) {
		if (err_out)
			*err_out = "Signer key path does not exist: " +
				   st.signer_key_path;
		return false;
	}
	if (!fs::exists(st.signer_cert_path)) {
		if (err_out)
			*err_out = "Signer certificate path does not exist: " +
				   st.signer_cert_path;
		return false;
	}

	const fs::path serve_root = get_serve_root_path(st.package_output_dir);
	const fs::path base_dir = serve_root / st.taxonomy.l1 / st.taxonomy.l2 /
				  st.taxonomy.l3;
	const fs::path manifest_path = base_dir / "manifest.json";

	std::error_code ec;
	fs::create_directories(base_dir, ec);
	if (ec) {
		if (err_out)
			*err_out = "Failed to create output directory: " +
				   base_dir.string() + " (" + ec.message() +
				   ")";
		return false;
	}

	Manifest existing_manifest;
	bool has_existing_manifest = false;
	bool existing_has_default = false;
	if (fs::exists(manifest_path, ec)) {
		ec.clear();
		std::string manifest_err;
		if (!load_manifest_file(manifest_path, &existing_manifest,
					&manifest_err)) {
			if (err_out)
				*err_out = manifest_err.empty()
					       ? ("Failed to parse existing "
						  "manifest: " +
						  manifest_path.string())
					       : manifest_err;
			return false;
		}
		has_existing_manifest = true;
		existing_has_default = manifest_has_device(existing_manifest,
							   "default");
	}

	if (!has_default && !existing_has_default) {
		if (err_out)
			*err_out = "Device ID 'default' is required.";
		return false;
	}

	const std::string release_created = st.created.empty()
						? now_utc_iso8601()
						: st.created;
	const std::string file_type = st.file_type.empty() ? "rauc_bundle"
							   : st.file_type;

	Manifest merged_manifest;
	if (has_existing_manifest)
		merged_manifest = std::move(existing_manifest);
	merged_manifest.manifest_version =
	    st.manifest_version.empty()
		? (merged_manifest.manifest_version.empty()
		       ? "9.9.9-test"
		       : merged_manifest.manifest_version)
		: st.manifest_version;
	if (merged_manifest.created.empty())
		merged_manifest.created = release_created;

	for (auto &rel : st.releases) {
		const fs::path device_dir = base_dir / rel.device_id;
		fs::create_directories(device_dir, ec);
		if (ec) {
			if (err_out)
				*err_out =
				    "Failed to create device directory: " +
				    device_dir.string() + " (" + ec.message() +
				    ")";
			return false;
		}

		if (rel.device_id == "default") {
			if (!clear_directory_contents(device_dir, err_out))
				return false;
		}

		std::unordered_set<std::string> used_names;
		for (const auto &entry :
		     fs::directory_iterator(device_dir, ec)) {
			if (ec)
				break;
			if (entry.is_regular_file())
				used_names.insert(
				    entry.path().filename().string());
		}
		if (ec) {
			if (err_out)
				*err_out = "Failed to read device directory: " +
					   device_dir.string() + " (" +
					   ec.message() + ")";
			return false;
		}

		for (auto &f : rel.files) {
			if (f.local_path.empty()) {
				if (err_out)
					*err_out =
					    "File entry missing local path.";
				return false;
			}

			const fs::path src = f.local_path;
			if (!fs::exists(src)) {
				if (err_out)
					*err_out = "File does not exist: " +
						   src.string();
				return false;
			}

			std::string desired = f.filename.empty()
						  ? src.filename().string()
						  : f.filename;
			std::string sanitized = sanitize_filename_component(
			    desired);
			std::string unique = sanitized;
			int counter = 1;
			while (used_names.count(unique) > 0)
				unique = sanitized + "_" +
					 std::to_string(counter++);
			used_names.insert(unique);

			if (unique != f.filename)
				log_line("Payload filename adjusted to: " +
					 unique);
			f.filename = unique;

			const fs::path dst = device_dir / unique;
			fs::copy_file(src, dst,
				      fs::copy_options::overwrite_existing, ec);
			if (ec) {
				if (err_out)
					*err_out = "Failed to copy file: " +
						   src.string();
				return false;
			}

			const auto dst_size = fs::file_size(dst, ec);
			if (ec) {
				if (err_out)
					*err_out =
					    "Failed to read file size: " +
					    dst.string();
				return false;
			}
			f.size = static_cast<uint64_t>(dst_size);

			const std::string sha = sha256_file_hex(dst.string());
			if (sha.empty()) {
				if (err_out)
					*err_out =
					    "Failed to compute SHA-256: " +
					    dst.string();
				return false;
			}
			f.sha256 = sha;
		}

		TargetRelease release;
		release.device_id = rel.device_id;
		release.release_name = st.release_name;
		release.release_version = st.release_version;
		release.created = release_created;
		release.files.reserve(rel.files.size());
		for (const auto &f : rel.files) {
			FileEntry entry;
			entry.file_type = file_type;
			entry.filename = f.filename.empty() ? "file"
							    : f.filename;
			entry.size = f.size;
			entry.sha256 = f.sha256;
			entry.path = rel.device_id + "/" + entry.filename;
			release.files.push_back(std::move(entry));
		}
		upsert_release(merged_manifest, rel.device_id, release);
	}

	if (!manifest_has_device(merged_manifest, "default")) {
		if (err_out)
			*err_out = "Device ID 'default' is required.";
		return false;
	}

	const std::string manifest_json = build_manifest_json(merged_manifest);
	if (!save_text_file(manifest_path.string(), manifest_json, err_out))
		return false;

	std::string sign_err;
	const fs::path sig_path = base_dir / "manifest.json.sig";
	if (!sign_file(manifest_path.string(), st.signer_key_path,
		       st.signer_cert_path, sig_path.string(),
		       st.signing_algorithm, &sign_err)) {
		if (err_out)
			*err_out = sign_err.empty() ? "Manifest signing failed."
						    : sign_err;
		return false;
	}

	const fs::path cert_dst = base_dir / "signer.crt";
	fs::copy_file(st.signer_cert_path, cert_dst,
		      fs::copy_options::overwrite_existing, ec);
	if (ec) {
		if (err_out)
			*err_out = "Failed to copy signer certificate: " +
				   ec.message();
		return false;
	}

	st.last_package_path = base_dir.string();
	st.last_saved_path = manifest_path.string();

	if (release_path_out)
		*release_path_out = base_dir.string();
	if (manifest_path_out)
		*manifest_path_out = manifest_path.string();

	const std::string base_url = build_device_base_url(srv, st.taxonomy);
	log_line("Device base URL set to: " + base_url);
	log_line("Manifest written to: " + manifest_path.string());
	return true;
}

void sync_server_doc_root(const ManifestState &st, HttpServerState &srv) {
	const std::string dir = st.package_output_dir.empty()
				    ? std::string(kDefaultServeRoot)
				    : st.package_output_dir;
	std::snprintf(srv.doc_root, sizeof(srv.doc_root), "%s", dir.c_str());
}

bool start_civetweb(const ManifestState &st, HttpServerState &srv) {
	namespace fs = std::filesystem;

	if (srv.running)
		return true;

	const fs::path root = srv.doc_root[0] ? fs::path(srv.doc_root)
					      : fs::path(".");
	if (!fs::exists(root)) {
		std::error_code ec;
		fs::create_directories(root, ec);
		if (ec) {
			srv.status = "Failed to prepare content root: " +
				     root.string();
			log_line(srv.status);
			return false;
		}
		log_line("Created content root directory: " + root.string());
	}

	if (!fs::is_directory(root)) {
		srv.status = "Content root is not a directory: " +
			     root.string();
		log_line(srv.status);
		return false;
	}

	if (srv.use_https) {
		if (st.tls_cert_path.empty() || st.tls_key_path.empty()) {
			srv.status = "TLS key/certificate missing. Configure "
				     "them before starting HTTPS.";
			log_line(srv.status);
			return false;
		}

		std::string err;
		if (!write_combined_pem(st.tls_cert_path, st.tls_key_path,
					&srv.combined_pem_path, &err)) {
			srv.status = err;
			log_line(srv.status);
			return false;
		}
	} else {
		srv.combined_pem_path.clear();
	}

	srv.listening_spec = build_listening_spec(srv);

	srv.mg_options.clear();
	srv.mg_options.push_back("document_root");
	srv.mg_options.push_back(srv.doc_root);
	srv.mg_options.push_back("listening_ports");
	srv.mg_options.push_back(srv.listening_spec.c_str());
	srv.mg_options.push_back("enable_directory_listing");
	srv.mg_options.push_back("no");
	if (srv.use_https) {
		srv.mg_options.push_back("ssl_certificate");
		srv.mg_options.push_back(srv.combined_pem_path.c_str());
	}
	srv.mg_options.push_back(nullptr);

	mg_callbacks callbacks{};
	callbacks.log_message = civetweb_log_callback;

	mg_init_data init_data{};
	init_data.callbacks = &callbacks;
	init_data.user_data = &srv;
	init_data.configuration_options = srv.mg_options.data();

	mg_error_data error_data{};
	char err_text[256]{};
	error_data.text = err_text;
	error_data.text_buffer_size = sizeof(err_text);

	srv.ctx = mg_start2(&init_data, &error_data);
	if (!srv.ctx) {
		const std::string detail = describe_civetweb_error(error_data);
		srv.status = "Failed to start CivetWeb server: " + detail;
		log_line(srv.status);
		if (srv.use_https && !srv.combined_pem_path.empty()) {
			std::error_code rm_ec;
			std::filesystem::remove(srv.combined_pem_path, rm_ec);
		}
		srv.combined_pem_path.clear();
		srv.mg_options.clear();
		return false;
	}

	srv.running = true;
	srv.started_at = std::chrono::steady_clock::now();

	std::ostringstream oss;
	const char *bind = srv.bind_address[0] ? srv.bind_address : "0.0.0.0";
	oss << (srv.use_https ? "HTTPS" : "HTTP") << " server listening on "
	    << bind << ":" << srv.port << " serving " << root.string();
	srv.status = oss.str();
	log_line(srv.status);
	return true;
}

void stop_civetweb(HttpServerState &srv) {
	if (srv.ctx) {
		mg_stop(srv.ctx);
		srv.ctx = nullptr;
	}

	if (!srv.combined_pem_path.empty()) {
		std::error_code ec;
		std::filesystem::remove(srv.combined_pem_path, ec);
		srv.combined_pem_path.clear();
	}

	srv.mg_options.clear();

	if (srv.running) {
		srv.running = false;
		srv.status = "HTTP(S) server stopped.";
		log_line(srv.status);
	}
}
