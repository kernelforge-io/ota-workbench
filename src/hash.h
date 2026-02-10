// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 Kernel Forge LLC

/**
 * @file hash.h
 * @brief SHA-256 hashing utilities for OTA update integrity.
 *
 * Provides functions for hashing files and printing SHA-256 digests.
 * Intended for use in manifest and payload validation in OTA update systems.
 *
 * @author Dustin Hoskins
 * @date 2025
 */

#ifndef HASH_H
#define HASH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Length of a SHA-256 digest, in bytes.
 */
#define SHA256_DIGEST_LEN 32

/**
 * @brief Calculate the SHA-256 hash of a file.
 *
 * Opens and reads the file at @p path, computes its SHA-256 hash, and
 * writes the 32-byte result to @p digest_out.
 *
 * @param path       Path to the file to hash.
 * @param digest_out Output buffer for SHA-256 digest (must be at least 32
 * bytes).
 * @return 0 on success, nonzero on error (see implementation for details).
 */
int sha256sum_file(const char *path, uint8_t *digest_out);

/**
 * @brief Print a SHA-256 digest as a hex string with a label.
 *
 * Outputs the hash in lowercase hexadecimal to stderr, optionally prefixed by
 * @p label.
 *
 * @param label Label to print before the hash (for context in logs).
 * @param hash  Pointer to SHA-256 hash bytes.
 * @param len   Length of the hash (should be 32 for SHA-256).
 */
void print_sha256sum(const char *label, const uint8_t *hash, size_t len);

#ifdef __cplusplus
}
#endif

#endif // HASH_H
