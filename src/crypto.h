// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 Kernel Forge LLC

#pragma once

#include <string>

enum class SignAlg {
	Ed25519,
	EcdsaP256Sha256,
	RsaPssSha256,
};

const char *sign_alg_config_value(SignAlg alg);
const char *sign_alg_display_name(SignAlg alg);
bool parse_sign_alg(const std::string &value, SignAlg *out);

bool sign_file(const std::string &file_path, const std::string &key_path,
	       const std::string &cert_path,
	       const std::string &signature_path, SignAlg alg,
	       std::string *err_out);

bool write_combined_pem(const std::string &cert_path,
			const std::string &key_path,
			std::string *combined_path_out, std::string *err_out);

std::string sha256_file_hex(const std::string &path);
std::string sha256_string_hex(const std::string &input);
std::string openssl_error_stack();
std::string openssl_last_error();
