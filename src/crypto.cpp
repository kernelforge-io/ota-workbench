// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 Kernel Forge LLC

#include "crypto.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <openssl/ec.h>
#include <openssl/opensslv.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/core_names.h>
#endif

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace {
struct Sha256Ctx {
	uint8_t data[64];
	uint32_t datalen;
	uint64_t bitlen;
	uint32_t state[8];
};

constexpr uint32_t k_sha256[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

inline uint32_t rotr32(uint32_t x, uint32_t n) {
	return (x >> n) | (x << (32 - n));
}
inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
	return (x & y) ^ (~x & z);
}
inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
	return (x & y) ^ (x & z) ^ (y & z);
}
inline uint32_t ep0(uint32_t x) {
	return rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22);
}
inline uint32_t ep1(uint32_t x) {
	return rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25);
}
inline uint32_t sig0(uint32_t x) {
	return rotr32(x, 7) ^ rotr32(x, 18) ^ (x >> 3);
}
inline uint32_t sig1(uint32_t x) {
	return rotr32(x, 17) ^ rotr32(x, 19) ^ (x >> 10);
}

void sha256_transform(Sha256Ctx &ctx, const uint8_t data[]) {
	uint32_t m[64];
	for (uint32_t i = 0, j = 0; i < 16; ++i, j += 4)
		m[i] = (static_cast<uint32_t>(data[j]) << 24) |
		       (static_cast<uint32_t>(data[j + 1]) << 16) |
		       (static_cast<uint32_t>(data[j + 2]) << 8) |
		       (static_cast<uint32_t>(data[j + 3]));

	for (uint32_t i = 16; i < 64; ++i)
		m[i] = sig1(m[i - 2]) + m[i - 7] + sig0(m[i - 15]) + m[i - 16];

	uint32_t a = ctx.state[0];
	uint32_t b = ctx.state[1];
	uint32_t c = ctx.state[2];
	uint32_t d = ctx.state[3];
	uint32_t e = ctx.state[4];
	uint32_t f = ctx.state[5];
	uint32_t g = ctx.state[6];
	uint32_t h = ctx.state[7];

	for (uint32_t i = 0; i < 64; ++i) {
		const uint32_t t1 = h + ep1(e) + ch(e, f, g) + k_sha256[i] +
				    m[i];
		const uint32_t t2 = ep0(a) + maj(a, b, c);
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}

	ctx.state[0] += a;
	ctx.state[1] += b;
	ctx.state[2] += c;
	ctx.state[3] += d;
	ctx.state[4] += e;
	ctx.state[5] += f;
	ctx.state[6] += g;
	ctx.state[7] += h;
}

void sha256_init(Sha256Ctx &ctx) {
	ctx.datalen = 0;
	ctx.bitlen = 0;
	ctx.state[0] = 0x6a09e667u;
	ctx.state[1] = 0xbb67ae85u;
	ctx.state[2] = 0x3c6ef372u;
	ctx.state[3] = 0xa54ff53au;
	ctx.state[4] = 0x510e527fu;
	ctx.state[5] = 0x9b05688cu;
	ctx.state[6] = 0x1f83d9abu;
	ctx.state[7] = 0x5be0cd19u;
}

void sha256_update(Sha256Ctx &ctx, const uint8_t *data, size_t len) {
	for (size_t i = 0; i < len; ++i) {
		ctx.data[ctx.datalen] = data[i];
		ctx.datalen++;
		if (ctx.datalen == 64) {
			sha256_transform(ctx, ctx.data);
			ctx.bitlen += 512;
			ctx.datalen = 0;
		}
	}
}

void sha256_final(Sha256Ctx &ctx, uint8_t hash[32]) {
	uint32_t i = ctx.datalen;

	if (ctx.datalen < 56) {
		ctx.data[i++] = 0x80;
		while (i < 56)
			ctx.data[i++] = 0x00;
	} else {
		ctx.data[i++] = 0x80;
		while (i < 64)
			ctx.data[i++] = 0x00;
		sha256_transform(ctx, ctx.data);
		std::memset(ctx.data, 0, 56);
	}

	ctx.bitlen += static_cast<uint64_t>(ctx.datalen) * 8;

	ctx.data[63] = static_cast<uint8_t>(ctx.bitlen);
	ctx.data[62] = static_cast<uint8_t>(ctx.bitlen >> 8);
	ctx.data[61] = static_cast<uint8_t>(ctx.bitlen >> 16);
	ctx.data[60] = static_cast<uint8_t>(ctx.bitlen >> 24);
	ctx.data[59] = static_cast<uint8_t>(ctx.bitlen >> 32);
	ctx.data[58] = static_cast<uint8_t>(ctx.bitlen >> 40);
	ctx.data[57] = static_cast<uint8_t>(ctx.bitlen >> 48);
	ctx.data[56] = static_cast<uint8_t>(ctx.bitlen >> 56);
	sha256_transform(ctx, ctx.data);

	for (i = 0; i < 4; ++i) {
		hash[i] = static_cast<uint8_t>((ctx.state[0] >> (24 - i * 8)) &
					       0x000000ffu);
		hash[i + 4] = static_cast<uint8_t>(
		    (ctx.state[1] >> (24 - i * 8)) & 0x000000ffu);
		hash[i + 8] = static_cast<uint8_t>(
		    (ctx.state[2] >> (24 - i * 8)) & 0x000000ffu);
		hash[i + 12] = static_cast<uint8_t>(
		    (ctx.state[3] >> (24 - i * 8)) & 0x000000ffu);
		hash[i + 16] = static_cast<uint8_t>(
		    (ctx.state[4] >> (24 - i * 8)) & 0x000000ffu);
		hash[i + 20] = static_cast<uint8_t>(
		    (ctx.state[5] >> (24 - i * 8)) & 0x000000ffu);
		hash[i + 24] = static_cast<uint8_t>(
		    (ctx.state[6] >> (24 - i * 8)) & 0x000000ffu);
		hash[i + 28] = static_cast<uint8_t>(
		    (ctx.state[7] >> (24 - i * 8)) & 0x000000ffu);
	}
}

std::string bytes_to_hex(const uint8_t *data, size_t len) {
	std::ostringstream oss;
	oss << std::hex << std::setfill('0');
	for (size_t i = 0; i < len; ++i)
		oss << std::setw(2) << static_cast<int>(data[i]);
	return oss.str();
}

std::string sha256_bytes_hex(const uint8_t *data, size_t len) {
	Sha256Ctx ctx{};
	sha256_init(ctx);
	if (data && len > 0)
		sha256_update(ctx, data, len);

	uint8_t hash[32]{};
	sha256_final(ctx, hash);
	return bytes_to_hex(hash, sizeof(hash));
}
} // namespace

std::string openssl_error_stack() {
	std::ostringstream oss;
	unsigned long err = 0;
	bool found = false;
	while ((err = ERR_get_error()) != 0) {
		char buf[256];
		ERR_error_string_n(err, buf, sizeof(buf));
		if (found)
			oss << " | ";
		oss << buf;
		found = true;
	}
	if (!found)
		return "OpenSSL error";
	return oss.str();
}

std::string openssl_last_error() {
	return openssl_error_stack();
}

std::string sha256_file_hex(const std::string &path) {
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return {};

	Sha256Ctx ctx{};
	sha256_init(ctx);

	std::array<uint8_t, 64 * 1024> buf{};
	while (f) {
		f.read(reinterpret_cast<char *>(buf.data()),
		       static_cast<std::streamsize>(buf.size()));
		const auto got = f.gcount();
		if (got > 0)
			sha256_update(ctx, buf.data(),
				      static_cast<size_t>(got));
	}

	uint8_t hash[32]{};
	sha256_final(ctx, hash);
	return bytes_to_hex(hash, sizeof(hash));
}

std::string sha256_string_hex(const std::string &input) {
	return sha256_bytes_hex(
	    reinterpret_cast<const uint8_t *>(input.data()), input.size());
}

namespace {
struct SignAlgInfo {
	SignAlg alg;
	const char *config;
	const char *display;
	const char *short_name;
};

constexpr SignAlgInfo kSignAlgInfo[] = {
    {SignAlg::Ed25519, "ed25519", "Ed25519 (recommended)", "Ed25519"},
    {SignAlg::EcdsaP256Sha256, "ecdsa_p256_sha256",
     "ECDSA P-256 + SHA-256", "ECDSA P-256 + SHA-256"},
    {SignAlg::RsaPssSha256, "rsa_pss_sha256",
     "RSA-PSS + SHA-256 (optional)", "RSA-PSS + SHA-256"},
};

const SignAlgInfo *find_sign_alg(SignAlg alg) {
	for (const auto &info : kSignAlgInfo) {
		if (info.alg == alg)
			return &info;
	}
	return nullptr;
}

const SignAlgInfo *find_sign_alg_by_config(const std::string &value) {
	for (const auto &info : kSignAlgInfo) {
		if (value == info.config)
			return &info;
	}
	return nullptr;
}

const char *key_type_name(int key_type) {
	switch (key_type) {
	case EVP_PKEY_ED25519:
		return "Ed25519";
	case EVP_PKEY_ED448:
		return "Ed448";
	case EVP_PKEY_EC:
		return "ECDSA";
	case EVP_PKEY_RSA:
		return "RSA";
	case EVP_PKEY_RSA_PSS:
		return "RSA-PSS";
	default:
		return "unknown";
	}
}

bool read_file_bin(const std::string &path,
		   std::vector<unsigned char> *out,
		   std::string *err_out) {
	if (!out)
		return false;
	out->clear();
	std::ifstream f(path, std::ios::binary);
	if (!f) {
		if (err_out)
			*err_out = "Failed to open manifest for signing: " +
				   path;
		return false;
	}
	f.seekg(0, std::ios::end);
	const std::streamsize size = f.tellg();
	if (size < 0) {
		if (err_out)
			*err_out = "Failed to read manifest size: " + path;
		return false;
	}
	out->resize(static_cast<size_t>(size));
	f.seekg(0, std::ios::beg);
	if (size > 0) {
		f.read(reinterpret_cast<char *>(out->data()), size);
		if (!f) {
			if (err_out)
				*err_out =
				    "Failed while reading manifest for signing: " +
				    path;
			return false;
		}
	}
	return true;
}

bool ensure_ec_p256(EVP_PKEY *pkey, std::string *err_out) {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
	char group_name[80] = {0};
	if (EVP_PKEY_get_utf8_string_param(pkey, OSSL_PKEY_PARAM_GROUP_NAME,
					   group_name, sizeof(group_name),
					   nullptr) != 1) {
		if (err_out)
			*err_out = "Failed to read EC key parameters: " +
				   openssl_error_stack();
		return false;
	}

	const std::string group(group_name);
	if (group == "prime256v1" || group == "secp256r1")
		return true;

	if (err_out) {
		*err_out =
		    "Selected signing algorithm requires an ECDSA P-256 key "
		    "(prime256v1/secp256r1), but key group is '" +
		    (group.empty() ? std::string("unknown") : group) + "'.";
	}
	return false;
#else
	const EC_KEY *ec = EVP_PKEY_get0_EC_KEY(pkey);
	if (!ec) {
		if (err_out)
			*err_out = "Signer key is not an EC key.";
		return false;
	}
	const EC_GROUP *group = EC_KEY_get0_group(ec);
	if (!group) {
		if (err_out)
			*err_out = "Failed to read EC key parameters.";
		return false;
	}
	const int curve = EC_GROUP_get_curve_name(group);
	if (curve != NID_X9_62_prime256v1) {
		if (err_out)
			*err_out =
		    "Selected signing algorithm requires an ECDSA P-256 key.";
		return false;
	}
	return true;
#endif
}

bool ensure_key_matches_alg(SignAlg alg, EVP_PKEY *pkey,
			    std::string *err_out) {
	const auto *info = find_sign_alg(alg);
	const char *alg_name =
	    info ? info->short_name : "selected signing algorithm";
	const int key_type = EVP_PKEY_base_id(pkey);
	switch (alg) {
	case SignAlg::Ed25519:
		if (key_type != EVP_PKEY_ED25519) {
			if (err_out)
				*err_out =
				    std::string("Selected signing algorithm ") +
				    alg_name +
				    " requires an Ed25519 key, but the signer key is " +
				    key_type_name(key_type) + ".";
			return false;
		}
		break;
	case SignAlg::EcdsaP256Sha256:
		if (key_type != EVP_PKEY_EC) {
			if (err_out)
				*err_out =
				    std::string("Selected signing algorithm ") +
				    alg_name +
				    " requires an ECDSA key, but the signer key is " +
				    key_type_name(key_type) + ".";
			return false;
		}
		if (!ensure_ec_p256(pkey, err_out))
			return false;
		break;
	case SignAlg::RsaPssSha256:
		if (key_type != EVP_PKEY_RSA &&
		    key_type != EVP_PKEY_RSA_PSS) {
			if (err_out)
				*err_out =
				    std::string("Selected signing algorithm ") +
				    alg_name +
				    " requires an RSA key, but the signer key is " +
				    key_type_name(key_type) + ".";
			return false;
		}
		break;
	default:
		if (err_out)
			*err_out = "Unknown signing algorithm selected.";
		return false;
	}
	return true;
}

bool configure_rsa_pss(EVP_PKEY_CTX *pctx, std::string *err_out) {
	if (!pctx) {
		if (err_out)
			*err_out = "Failed to configure RSA-PSS context.";
		return false;
	}
	if (EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) <= 0) {
		if (err_out)
			*err_out = "Failed to set RSA-PSS padding: " +
				   openssl_error_stack();
		return false;
	}
	if (EVP_PKEY_CTX_set_rsa_mgf1_md(pctx, EVP_sha256()) <= 0) {
		if (err_out)
			*err_out = "Failed to set RSA-PSS MGF1 digest: " +
				   openssl_error_stack();
		return false;
	}
	if (EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, -1) <= 0) {
		if (err_out)
			*err_out = "Failed to set RSA-PSS salt length: " +
				   openssl_error_stack();
		return false;
	}
	return true;
}
} // namespace

const char *sign_alg_config_value(SignAlg alg) {
	const auto *info = find_sign_alg(alg);
	return info ? info->config : "ed25519";
}

const char *sign_alg_display_name(SignAlg alg) {
	const auto *info = find_sign_alg(alg);
	return info ? info->display : "Ed25519 (recommended)";
}

bool parse_sign_alg(const std::string &value, SignAlg *out) {
	if (!out)
		return false;
	const auto *info = find_sign_alg_by_config(value);
	if (!info)
		return false;
	*out = info->alg;
	return true;
}

bool sign_file(const std::string &file_path, const std::string &key_path,
	       const std::string &cert_path,
	       const std::string &signature_path, SignAlg alg,
	       std::string *err_out) {
	if (file_path.empty() || key_path.empty()) {
		if (err_out)
			*err_out = "Signer key or manifest path missing.";
		return false;
	}

	ERR_clear_error();

	BIO *key_bio = BIO_new_file(key_path.c_str(), "r");
	if (!key_bio) {
		if (err_out)
			*err_out = "Failed to load signer key: " + key_path;
		return false;
	}

	EVP_PKEY *pkey = PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr,
						 nullptr);
	BIO_free(key_bio);
	if (!pkey) {
		if (err_out)
			*err_out = "Failed to parse signer key: " +
				   openssl_last_error();
		return false;
	}

	X509 *cert = nullptr;
	if (!cert_path.empty()) {
		BIO *cert_bio = BIO_new_file(cert_path.c_str(), "r");
		if (!cert_bio) {
			if (err_out)
				*err_out = "Failed to load signer certificate: " +
					   cert_path;
			EVP_PKEY_free(pkey);
			return false;
		}

		cert = PEM_read_bio_X509(cert_bio, nullptr, nullptr, nullptr);
		BIO_free(cert_bio);
		if (!cert) {
			if (err_out)
				*err_out = "Failed to parse signer certificate: " +
					   openssl_error_stack();
			EVP_PKEY_free(pkey);
			return false;
		}

		if (X509_check_private_key(cert, pkey) != 1) {
			if (err_out)
				*err_out = "Signer key does not match certificate: " +
					   openssl_error_stack();
			X509_free(cert);
			EVP_PKEY_free(pkey);
			return false;
		}
	}

	std::string err_msg;
	if (!ensure_key_matches_alg(alg, pkey, &err_msg)) {
		if (err_out)
			*err_out = err_msg;
		if (cert)
			X509_free(cert);
		EVP_PKEY_free(pkey);
		return false;
	}

	EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
	if (!mdctx) {
		if (err_out)
			*err_out = "Failed to allocate OpenSSL digest context.";
		if (cert)
			X509_free(cert);
		EVP_PKEY_free(pkey);
		return false;
	}

	bool ok = false;
	std::vector<unsigned char> sig;
	size_t sig_len = 0;

	if (alg == SignAlg::Ed25519) {
		std::vector<unsigned char> data;
		if (!read_file_bin(file_path, &data, &err_msg)) {
			ok = false;
		} else if (EVP_DigestSignInit(mdctx, nullptr, nullptr, nullptr,
					      pkey) != 1) {
			err_msg = "EVP_DigestSignInit failed: " +
				  openssl_error_stack();
		} else {
			const unsigned char *data_ptr =
			    data.empty() ? nullptr : data.data();
			if (EVP_DigestSign(mdctx, nullptr, &sig_len, data_ptr,
					   data.size()) != 1) {
				err_msg = "EVP_DigestSign (size) failed: " +
					  openssl_error_stack();
			} else {
				sig.resize(sig_len);
				if (EVP_DigestSign(mdctx, sig.data(), &sig_len,
						   data_ptr, data.size()) !=
				    1) {
					err_msg = "EVP_DigestSign failed: " +
						  openssl_error_stack();
				} else if (sig_len != 64) {
					err_msg =
					    "Ed25519 signature length is unexpected.";
				}
			}
		}
	} else {
		EVP_PKEY_CTX *pctx = nullptr;
		if (EVP_DigestSignInit(mdctx, &pctx, EVP_sha256(), nullptr,
				       pkey) != 1) {
			err_msg = "EVP_DigestSignInit failed: " +
				  openssl_error_stack();
		} else if (alg == SignAlg::RsaPssSha256 &&
			   !configure_rsa_pss(pctx, &err_msg)) {
		} else {
			std::ifstream manifest(file_path, std::ios::binary);
			if (!manifest) {
				err_msg =
				    "Failed to open manifest for signing: " +
				    file_path;
			} else {
				std::array<unsigned char, 64 * 1024> buf{};
				while (manifest) {
					manifest.read(
					    reinterpret_cast<char *>(
						buf.data()),
					    static_cast<std::streamsize>(
						buf.size()));
					const auto got = manifest.gcount();
					if (got > 0) {
						if (EVP_DigestSignUpdate(
							mdctx, buf.data(),
							static_cast<size_t>(
							    got)) != 1) {
							err_msg =
							    "EVP_DigestSignUpdate failed: " +
							    openssl_error_stack();
							break;
						}
					}
				}

				if (err_msg.empty() && manifest.bad())
					err_msg =
					    "Failed while reading manifest for signing: " +
					    file_path;

				if (err_msg.empty()) {
					if (EVP_DigestSignFinal(mdctx, nullptr,
								&sig_len) !=
					    1) {
						err_msg =
						    "EVP_DigestSignFinal (size) failed: " +
						    openssl_error_stack();
					} else {
						sig.resize(sig_len);
						if (EVP_DigestSignFinal(
							mdctx, sig.data(),
							&sig_len) != 1) {
							err_msg =
							    "EVP_DigestSignFinal failed: " +
							    openssl_error_stack();
						}
					}
				}
			}
		}
	}

	if (err_msg.empty()) {
		std::ofstream out(signature_path,
				  std::ios::binary | std::ios::trunc);
		if (!out) {
			err_msg =
			    "Failed to open signature path for writing: " +
			    signature_path;
		} else {
			out.write(reinterpret_cast<const char *>(sig.data()),
				  static_cast<std::streamsize>(sig_len));
			if (!out)
				err_msg = "Failed while writing signature: " +
					  signature_path;
			else
				ok = true;
		}
	}

	if (!ok && err_out)
		*err_out = err_msg.empty() ? "Manifest signing failed."
					   : err_msg;

	EVP_MD_CTX_free(mdctx);
	if (cert)
		X509_free(cert);
	EVP_PKEY_free(pkey);
	return ok;
}

bool write_combined_pem(const std::string &cert_path,
			const std::string &key_path,
			std::string *combined_path_out, std::string *err_out) {
	namespace fs = std::filesystem;

	if (cert_path.empty() || key_path.empty()) {
		if (err_out)
			*err_out = "TLS key or certificate path missing.";
		return false;
	}

	std::ifstream cert(cert_path, std::ios::binary);
	if (!cert) {
		if (err_out)
			*err_out = "Failed to open TLS certificate: " +
				   cert_path;
		return false;
	}
	std::ostringstream cert_buf;
	cert_buf << cert.rdbuf();
	if (!cert) {
		if (err_out)
			*err_out = "Failed while reading TLS certificate: " +
				   cert_path;
		return false;
	}

	std::ifstream key(key_path, std::ios::binary);
	if (!key) {
		if (err_out)
			*err_out = "Failed to open TLS key: " + key_path;
		return false;
	}
	std::ostringstream key_buf;
	key_buf << key.rdbuf();
	if (!key) {
		if (err_out)
			*err_out = "Failed while reading TLS key: " + key_path;
		return false;
	}

	const fs::path temp_dir = fs::temp_directory_path();
	const auto stamp =
	    std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path combined_path = temp_dir /
				       ("ota-workbench-" +
					std::to_string(stamp) + ".pem");

	std::ofstream out(combined_path, std::ios::binary | std::ios::trunc);
	if (!out) {
		if (err_out)
			*err_out = "Failed to create combined PEM: " +
				   combined_path.string();
		return false;
	}

	const std::string cert_data = cert_buf.str();
	const std::string key_data = key_buf.str();

	out.write(cert_data.data(),
		  static_cast<std::streamsize>(cert_data.size()));
	if (!cert_data.empty() && cert_data.back() != '\n')
		out.put('\n');
	out.write(key_data.data(),
		  static_cast<std::streamsize>(key_data.size()));

	if (!out) {
		if (err_out)
			*err_out = "Failed while writing combined PEM: " +
				   combined_path.string();
		return false;
	}

#if !defined(_WIN32)
	::chmod(combined_path.c_str(), S_IRUSR | S_IWUSR);
#endif

	if (combined_path_out)
		*combined_path_out = combined_path.string();
	return true;
}
