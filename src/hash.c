// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 Kernel Forge LLC

#include "hash.h"
#include <openssl/sha.h>
#include <stdio.h>

/* ------------------------------------------------------------------------ */
int sha256sum_file(const char *path, uint8_t *digest_out) {
	fprintf(stderr, "sha256sum_file: %s\n", path);
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return -1;

	SHA256_CTX ctx;
	SHA256_Init(&ctx);

	unsigned char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		SHA256_Update(&ctx, buf, n);
	}

	// Check for read errors before closing the file!
	if (ferror(fp)) {
		fclose(fp);
		return -2;
	}
	fclose(fp);

	SHA256_Final(digest_out, &ctx);
	return 0;
}

/* ------------------------------------------------------------------------ */
void print_sha256sum(const char *label, const uint8_t *hash, size_t len) {
	fprintf(stderr, "%s= ", label);
	for (size_t i = 0; i < len; ++i)
		fprintf(stderr, "%02x", hash[i]);
	fprintf(stderr, "\n");
}
