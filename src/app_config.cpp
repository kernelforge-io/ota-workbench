// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 Kernel Forge LLC

#include "app_config.h"

#include "ini.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

#include "log.h"
#include "manifest.h"

std::filesystem::path get_config_directory() {
	namespace fs = std::filesystem;

#if defined(_WIN32)
	if (const char *appdata = std::getenv("APPDATA")) {
		if (*appdata)
			return fs::path(appdata) / "ota-workbench";
	}
	if (const char *user = std::getenv("USERPROFILE")) {
		if (*user)
			return fs::path(user) / "ota-workbench";
	}
#else
	if (const char *xdg = std::getenv("XDG_CONFIG_HOME")) {
		if (*xdg)
			return fs::path(xdg) / "ota-workbench";
	}
	if (const char *home = std::getenv("HOME")) {
		if (*home)
			return fs::path(home) / ".config" / "ota-workbench";
	}
#endif

	return fs::path(".");
}

std::filesystem::path get_config_file_path() {
	return get_config_directory() / "ota-workbench.conf";
}

namespace {
constexpr const char *kDefaultConfigTemplate =
    "; ota-workbench configuration\n"
    "[signing]\n"
    "; Set these to your signer private key and certificate files.\n"
    "signer_key=\n"
    "signer_cert=\n"
    "algorithm=ed25519\n"
    "[tls]\n"
    "; Set these to your TLS private key and certificate files.\n"
    "tls_key=\n"
    "tls_cert=\n"
    "[output]\n"
    "package_dir=server\n"
    "[ui]\n"
    "last_l1=\n"
    "last_l2=\n"
    "last_l3=\n"
    "last_tab_index=0\n"
    "[taxonomy]\n"
    "l1_label=Fleet Group\n"
    "l2_label=Sub Group\n"
    "l3_label=Function\n"
    "device_selector_label=Device ID\n";

int config_ini_handler(void *user, const char *section, const char *name,
		       const char *value) {
	if (!section || !name || !value)
		return 1;

	auto *cfg = static_cast<AppConfig *>(user);

	if (std::strcmp(section, "signing") == 0) {
		if (std::strcmp(name, "signer_key") == 0)
			cfg->signer_key_path = value;
		else if (std::strcmp(name, "signer_cert") == 0)
			cfg->signer_cert_path = value;
		else if (std::strcmp(name, "algorithm") == 0) {
			SignAlg parsed = cfg->signing_algorithm;
			if (parse_sign_alg(value, &parsed))
				cfg->signing_algorithm = parsed;
		}
	} else if (std::strcmp(section, "tls") == 0) {
		if (std::strcmp(name, "tls_key") == 0)
			cfg->tls_key_path = value;
		else if (std::strcmp(name, "tls_cert") == 0)
			cfg->tls_cert_path = value;
	} else if (std::strcmp(section, "output") == 0) {
		if (std::strcmp(name, "package_dir") == 0)
			cfg->package_output_dir = value;
	} else if (std::strcmp(section, "ui") == 0) {
		if (std::strcmp(name, "last_l1") == 0)
			cfg->last_l1 = value;
		else if (std::strcmp(name, "last_l2") == 0)
			cfg->last_l2 = value;
		else if (std::strcmp(name, "last_l3") == 0)
			cfg->last_l3 = value;
		else if (std::strcmp(name, "last_device_group") == 0 &&
			 cfg->last_l1.empty())
			cfg->last_l1 = value;
		else if (std::strcmp(name, "last_tab_index") == 0)
			cfg->last_tab_index = std::atoi(value);
	} else if (std::strcmp(section, "taxonomy") == 0) {
		if (std::strcmp(name, "l1_label") == 0)
			cfg->taxonomy_labels.l1_label = value;
		else if (std::strcmp(name, "l2_label") == 0)
			cfg->taxonomy_labels.l2_label = value;
		else if (std::strcmp(name, "l3_label") == 0)
			cfg->taxonomy_labels.l3_label = value;
		else if (std::strcmp(name, "device_selector_label") == 0)
			cfg->taxonomy_labels.device_selector_label = value;
	}

	return 1;
}

bool ensure_default_config_exists(const std::filesystem::path &config_path) {
	namespace fs = std::filesystem;

	const auto config_dir = config_path.parent_path();
	if (!config_dir.empty()) {
		std::error_code dir_ec;
		const bool created_dir =
		    fs::create_directories(config_dir, dir_ec);
		if (!created_dir && dir_ec) {
			const std::string msg =
			    "Failed to create config directory: " +
			    config_dir.string() + " (" +
			    dir_ec.message() + ")";
			log_line(msg);
			std::fprintf(stderr, "%s\n", msg.c_str());
			return false;
		}
#if !defined(_WIN32)
		if (created_dir) {
			std::error_code perms_ec;
			fs::permissions(config_dir, fs::perms::owner_all,
					fs::perm_options::replace,
					perms_ec);
			if (perms_ec) {
				const std::string msg =
				    "Failed to set config directory permissions: " +
				    config_dir.string() + " (" +
				    perms_ec.message() + ")";
				log_line(msg);
				std::fprintf(stderr, "%s\n", msg.c_str());
				return false;
			}
		}
#endif
	}

	std::ofstream out(config_path, std::ios::binary | std::ios::out |
				  std::ios::trunc);
	if (!out) {
		const std::string msg =
		    "Failed to create default config at " +
		    config_path.string();
		log_line(msg);
		std::fprintf(stderr, "%s\n", msg.c_str());
		return false;
	}

	out << kDefaultConfigTemplate;
	out.close();

	if (!out) {
		const std::string msg =
		    "Failed to write default config at " +
		    config_path.string();
		log_line(msg);
		std::fprintf(stderr, "%s\n", msg.c_str());
		return false;
	}

#if !defined(_WIN32)
	std::error_code perms_ec;
	fs::permissions(config_path,
			fs::perms::owner_read | fs::perms::owner_write,
			fs::perm_options::replace, perms_ec);
	if (perms_ec) {
		const std::string msg =
		    "Failed to set config file permissions: " +
		    config_path.string() + " (" +
		    perms_ec.message() + ")";
		log_line(msg);
		std::fprintf(stderr, "%s\n", msg.c_str());
		return false;
	}
#endif

	log_line("Config not found; created default at " +
		 config_path.string());
	return true;
}
} // namespace

bool load_app_config(AppConfig &cfg) {
	const auto config_path = get_config_file_path();
	if (!std::filesystem::exists(config_path) &&
	    !ensure_default_config_exists(config_path))
		return false;

	const int rc = ini_parse(config_path.string().c_str(),
				 config_ini_handler, &cfg);
	if (rc == -1) {
		log_line("Failed to open config file: " + config_path.string());
	} else if (rc > 0) {
		log_line("Config parse error in " + config_path.string() +
			 " at line " + std::to_string(rc));
	}

	return true;
}

bool save_app_config(const AppConfig &cfg) {
	namespace fs = std::filesystem;

	const auto config_path = get_config_file_path();
	const auto dir = config_path.parent_path();

	if (!dir.empty()) {
		std::error_code ec;
		fs::create_directories(dir, ec);
		if (ec) {
			log_line("Failed to create config directory: " +
				 dir.string());
			return false;
		}
	}

	std::ofstream out(config_path, std::ios::binary | std::ios::trunc);
	if (!out) {
		log_line("Failed to open config file for writing: " +
			 config_path.string());
		return false;
	}

	out << "; ota-workbench configuration\n";
	out << "[signing]\n";
	out << "signer_key=" << cfg.signer_key_path << "\n";
	out << "signer_cert=" << cfg.signer_cert_path << "\n";
	out << "algorithm=" << sign_alg_config_value(cfg.signing_algorithm)
	    << "\n";
	out << "[tls]\n";
	out << "tls_key=" << cfg.tls_key_path << "\n";
	out << "tls_cert=" << cfg.tls_cert_path << "\n";
	out << "[output]\n";
	out << "package_dir=" << cfg.package_output_dir << "\n";
	out << "[ui]\n";
	out << "last_l1=" << cfg.last_l1 << "\n";
	out << "last_l2=" << cfg.last_l2 << "\n";
	out << "last_l3=" << cfg.last_l3 << "\n";
	out << "last_tab_index=" << cfg.last_tab_index << "\n";
	out << "[taxonomy]\n";
	out << "l1_label=" << cfg.taxonomy_labels.l1_label << "\n";
	out << "l2_label=" << cfg.taxonomy_labels.l2_label << "\n";
	out << "l3_label=" << cfg.taxonomy_labels.l3_label << "\n";
	out << "device_selector_label="
	    << cfg.taxonomy_labels.device_selector_label << "\n";

	if (!out) {
		log_line("Failed to write config file: " +
			 config_path.string());
		return false;
	}

	return true;
}

void persist_configuration(const ManifestState &st, AppConfig &cfg) {
	update_app_config_from_manifest(st, cfg);
	save_app_config(cfg);
}
