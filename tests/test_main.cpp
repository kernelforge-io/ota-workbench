// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 Kernel Forge LLC

#include "crypto.h"
#include "manifest.h"

#include <cstddef>
#include <iostream>
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
} // namespace

int main() {
	const std::vector<TestCase> tests = {
	    {"Manifest parse/serialize round-trip",
	     test_manifest_parse_serialize_round_trip},
	    {"Manifest upsert/lookup", test_manifest_upsert_and_lookup_logic},
	    {"SHA-256 known vector", test_sha256_known_vector},
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
