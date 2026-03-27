// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 Kernel Forge LLC

#include "app_config.h"
#include "crypto.h"
#include "manifest.h"

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
struct TestCase {
	const char *name;
	bool (*run)(std::string *err_out);
};

bool expect_true(bool condition, const std::string &message,
		 std::string *err_out) {
	if (condition)
		return true;
	if (err_out)
		*err_out = message;
	return false;
}

bool expect_string_eq(const std::string &lhs, const std::string &rhs,
		      const std::string &label, std::string *err_out) {
	if (lhs == rhs)
		return true;
	if (err_out) {
		*err_out = label + " mismatch. Expected '" + rhs + "', got '" +
			   lhs + "'.";
	}
	return false;
}

bool compare_file_entry(const FileEntry &lhs, const FileEntry &rhs,
			const std::string &prefix, std::string *err_out) {
	if (!expect_string_eq(lhs.file_type, rhs.file_type,
			      prefix + " file_type", err_out))
		return false;
	if (!expect_string_eq(lhs.filename, rhs.filename,
			      prefix + " filename", err_out))
		return false;
	if (!expect_true(lhs.size == rhs.size, prefix + " size mismatch.",
			 err_out))
		return false;
	if (!expect_string_eq(lhs.sha256, rhs.sha256, prefix + " sha256",
			      err_out))
		return false;
	if (!expect_string_eq(lhs.path, rhs.path, prefix + " path", err_out))
		return false;
	return true;
}

bool compare_release(const TargetRelease &lhs, const TargetRelease &rhs,
		     std::size_t index, std::string *err_out) {
	const std::string prefix = "Release[" + std::to_string(index) + "]";
	if (!expect_string_eq(lhs.device_id, rhs.device_id,
			      prefix + " device_id", err_out))
		return false;
	if (!expect_string_eq(lhs.release_name, rhs.release_name,
			      prefix + " release_name", err_out))
		return false;
	if (!expect_string_eq(lhs.release_version, rhs.release_version,
			      prefix + " release_version", err_out))
		return false;
	if (!expect_string_eq(lhs.created, rhs.created,
			      prefix + " created", err_out))
		return false;
	if (!expect_true(lhs.files.size() == rhs.files.size(),
			 prefix + " file count mismatch.", err_out))
		return false;

	for (std::size_t file_index = 0; file_index < lhs.files.size();
	     ++file_index) {
		if (!compare_file_entry(
			lhs.files[file_index], rhs.files[file_index],
			prefix + " files[" + std::to_string(file_index) + "]",
			err_out))
			return false;
	}

	return true;
}

bool compare_manifest(const Manifest &lhs, const Manifest &rhs,
		      std::string *err_out) {
	if (!expect_string_eq(lhs.manifest_version, rhs.manifest_version,
			      "manifest_version", err_out))
		return false;
	if (!expect_string_eq(lhs.created, rhs.created, "created", err_out))
		return false;
	if (!expect_true(lhs.releases.size() == rhs.releases.size(),
			 "Release count mismatch.", err_out))
		return false;

	for (std::size_t release_index = 0; release_index < lhs.releases.size();
	     ++release_index) {
		if (!compare_release(lhs.releases[release_index],
				     rhs.releases[release_index], release_index,
				     err_out))
			return false;
	}

	return true;
}

bool test_manifest_parse_serialize_round_trip(std::string *err_out) {
	const std::string input_json = R"JSON(
{
  "manifest_version": "1.2.3",
  "created": "2026-02-08T00:00:00Z",
  "releases": [
    {
      "device_id": "default",
      "release_name": "portfolio-release",
      "release_version": "9.8.7",
      "created": "2026-02-08T00:00:00Z",
      "files": [
        {
          "file_type": "rauc_bundle",
          "filename": "update-a.raucb",
          "size": 123,
          "sha256": "1111111111111111111111111111111111111111111111111111111111111111",
          "path": "default/update-a.raucb"
        },
        {
          "file_type": "rauc_bundle",
          "filename": "update-b.raucb",
          "size": 456,
          "sha256": "2222222222222222222222222222222222222222222222222222222222222222",
          "path": "default/update-b.raucb"
        }
      ]
    }
  ]
}
)JSON";

	Manifest parsed;
	std::string parse_err;
	if (!parse_manifest_json(input_json, &parsed, &parse_err)) {
		if (err_out)
			*err_out = "Initial parse failed: " + parse_err;
		return false;
	}

	const std::string serialized = build_manifest_json(parsed);
	if (!expect_true(!serialized.empty(),
			 "build_manifest_json returned an empty string.",
			 err_out))
		return false;

	Manifest reparsed;
	std::string reparsed_err;
	if (!parse_manifest_json(serialized, &reparsed, &reparsed_err)) {
		if (err_out)
			*err_out =
			    "Parse after serialization failed: " + reparsed_err;
		return false;
	}

	return compare_manifest(parsed, reparsed, err_out);
}

bool test_manifest_upsert_and_lookup_logic(std::string *err_out) {
	Manifest manifest;

	TargetRelease first;
	first.device_id = "device-a";
	first.release_name = "rel";
	first.release_version = "1.0.0";
	first.created = "2026-02-08T00:00:00Z";
	first.files.push_back(
	    FileEntry{"rauc_bundle", "old.raucb", 10, "hash-old",
		      "device-a/old.raucb"});

	upsert_release(manifest, first.device_id, first);
	if (!expect_true(manifest.releases.size() == 1,
			 "First upsert should create one release.", err_out))
		return false;
	if (!expect_true(find_release_index_by_device_id(manifest, "device-a") ==
			     0,
			 "Expected device-a at index 0.", err_out))
		return false;
	if (!expect_true(manifest_has_device(manifest, "device-a"),
			 "manifest_has_device should find device-a.", err_out))
		return false;
	if (!expect_true(!manifest_has_device(manifest, "device-b"),
			 "manifest_has_device should not find device-b.",
			 err_out))
		return false;

	TargetRelease replacement;
	replacement.device_id = "device-a";
	replacement.release_name = "rel";
	replacement.release_version = "2.0.0";
	replacement.created = "2026-02-09T00:00:00Z";
	replacement.files.push_back(FileEntry{"rauc_bundle", "new.raucb", 99,
					      "hash-new",
					      "device-a/new.raucb"});

	upsert_release(manifest, replacement.device_id, replacement);
	if (!expect_true(manifest.releases.size() == 1,
			 "Replacement upsert should not add a second release.",
			 err_out))
		return false;

	const TargetRelease &stored = manifest.releases[0];
	if (!expect_true(stored.release_version == "2.0.0",
			 "Release version was not replaced.", err_out))
		return false;
	if (!expect_true(stored.files.size() == 1,
			 "Expected one file after replacement.", err_out))
		return false;
	if (!expect_true(stored.files[0].filename == "new.raucb",
			 "Expected replacement file entry.", err_out))
		return false;

	return true;
}

bool test_sha256_known_vector(std::string *err_out) {
	const std::string digest = sha256_string_hex("abc");
	const std::string expected =
	    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	return expect_string_eq(digest, expected,
				"SHA-256('abc') known vector", err_out);
}

bool set_env_var(const char *name, const std::string &value) {
#if defined(_WIN32)
	return _putenv_s(name, value.c_str()) == 0;
#else
	return setenv(name, value.c_str(), 1) == 0;
#endif
}

bool unset_env_var(const char *name) {
#if defined(_WIN32)
	return _putenv_s(name, "") == 0;
#else
	return unsetenv(name) == 0;
#endif
}

class ScopedEnvVar {
      public:
	ScopedEnvVar(const char *name, const std::string &value) :
	    name_(name) {
		const char *existing = std::getenv(name_);
		if (existing != nullptr) {
			had_original_ = true;
			original_ = existing;
		}
		set_env_var(name_, value);
	}

	~ScopedEnvVar() {
		if (had_original_) {
			set_env_var(name_, original_);
		} else {
			unset_env_var(name_);
		}
	}

      private:
	const char *name_;
	bool had_original_ = false;
	std::string original_;
};

std::filesystem::path make_temp_root(const std::string &label) {
	namespace fs = std::filesystem;
	const auto ticks =
	    std::chrono::steady_clock::now().time_since_epoch().count();
	return fs::temp_directory_path() /
	       ("ota-workbench-test-" + label + "-" +
		std::to_string(static_cast<long long>(ticks)));
}

bool test_missing_config_creates_file_and_continues(std::string *err_out) {
	namespace fs = std::filesystem;

	const fs::path temp_root = make_temp_root("missing");
	ScopedEnvVar xdg_config_home("XDG_CONFIG_HOME", temp_root.string());
	ScopedEnvVar home("HOME", "");

	AppConfig cfg;
	const fs::path config_path = get_config_file_path();
	if (!expect_true(!fs::exists(config_path),
			 "Config path unexpectedly exists before test.",
			 err_out))
		return false;

	if (!expect_true(load_app_config(cfg),
			 "load_app_config should succeed when creating default config.",
			 err_out))
		return false;

	if (!expect_true(fs::exists(config_path),
			 "Default config file was not created.", err_out))
		return false;
	if (!expect_true(cfg.package_output_dir == "server",
			 "Expected parser to preserve default package_dir.",
			 err_out))
		return false;

	std::error_code cleanup_ec;
	fs::remove_all(temp_root, cleanup_ec);
	return true;
}

bool test_existing_config_is_untouched(std::string *err_out) {
	namespace fs = std::filesystem;

	const fs::path temp_root = make_temp_root("existing");
	ScopedEnvVar xdg_config_home("XDG_CONFIG_HOME", temp_root.string());
	ScopedEnvVar home("HOME", "");

	const fs::path config_path = get_config_file_path();
	const fs::path config_dir = config_path.parent_path();
	std::error_code ec;
	fs::create_directories(config_dir, ec);
	if (!expect_true(!ec, "Failed to create test config directory.",
			 err_out))
		return false;

	const std::string original =
	    "[output]\n"
	    "package_dir=custom-package-dir\n"
	    "[signing]\n"
	    "algorithm=ed25519\n";
	{
		std::ofstream out(config_path, std::ios::binary | std::ios::trunc);
		if (!expect_true(static_cast<bool>(out),
				 "Failed to create existing config fixture.",
				 err_out))
			return false;
		out << original;
	}

	AppConfig cfg;
	if (!expect_true(load_app_config(cfg),
			 "load_app_config should succeed with existing config.",
			 err_out))
		return false;

	std::ifstream in(config_path, std::ios::binary);
	std::string after((std::istreambuf_iterator<char>(in)),
			  std::istreambuf_iterator<char>());
	if (!expect_string_eq(after, original,
			      "Existing config content", err_out))
		return false;
	if (!expect_string_eq(cfg.package_output_dir, "custom-package-dir",
			      "Loaded package_dir", err_out))
		return false;

	fs::remove_all(temp_root, ec);
	return true;
}

bool test_missing_config_directory_is_created(std::string *err_out) {
	namespace fs = std::filesystem;

	const fs::path temp_root = make_temp_root("mkdir");
	const fs::path config_home = temp_root / "a" / "b" / "c";
	ScopedEnvVar xdg_config_home("XDG_CONFIG_HOME",
				     config_home.string());
	ScopedEnvVar home("HOME", "");

	AppConfig cfg;
	const fs::path config_path = get_config_file_path();
	const fs::path config_dir = config_path.parent_path();
	if (!expect_true(!fs::exists(config_dir),
			 "Config directory unexpectedly exists before test.",
			 err_out))
		return false;

	if (!expect_true(load_app_config(cfg),
			 "load_app_config should create missing config directory.",
			 err_out))
		return false;
	if (!expect_true(fs::exists(config_dir),
			 "Missing config directory was not created.",
			 err_out))
		return false;

	std::error_code cleanup_ec;
	fs::remove_all(temp_root, cleanup_ec);
	return true;
}

bool test_created_config_permissions_are_restrictive(std::string *err_out) {
	namespace fs = std::filesystem;

	const fs::path temp_root = make_temp_root("perms");
	ScopedEnvVar xdg_config_home("XDG_CONFIG_HOME", temp_root.string());
	ScopedEnvVar home("HOME", "");

	AppConfig cfg;
	const fs::path config_path = get_config_file_path();
	if (!expect_true(load_app_config(cfg),
			 "load_app_config should create config before permissions check.",
			 err_out))
		return false;

#if !defined(_WIN32)
	std::error_code ec;
	const auto perms = fs::status(config_path, ec).permissions();
	if (!expect_true(!ec, "Failed to stat created config file.", err_out))
		return false;
	if (!expect_true((perms & fs::perms::others_read) ==
				 fs::perms::none,
			 "Config file should not be world-readable.", err_out))
		return false;
#endif

	std::error_code cleanup_ec;
	fs::remove_all(temp_root, cleanup_ec);
	return true;
}
} // namespace

int main() {
	const std::vector<TestCase> tests = {
	    {"Manifest parse/serialize round-trip",
	     test_manifest_parse_serialize_round_trip},
	    {"Manifest upsert/lookup", test_manifest_upsert_and_lookup_logic},
	    {"SHA-256 known vector", test_sha256_known_vector},
	    {"Missing config creates file and continues",
	     test_missing_config_creates_file_and_continues},
	    {"Existing config remains untouched",
	     test_existing_config_is_untouched},
	    {"Missing config directory is created",
	     test_missing_config_directory_is_created},
	    {"Created config permissions are restrictive",
	     test_created_config_permissions_are_restrictive},
	};

	int failed = 0;
	for (const TestCase &test : tests) {
		std::string err;
		const bool ok = test.run(&err);
		if (ok) {
			std::cout << "[PASS] " << test.name << "\n";
		} else {
			std::cout << "[FAIL] " << test.name;
			if (!err.empty())
				std::cout << ": " << err;
			std::cout << "\n";
			failed++;
		}
	}

	if (failed == 0) {
		std::cout << "All tests passed.\n";
		return 0;
	}

	std::cout << failed << " test(s) failed.\n";
	return 1;
}
