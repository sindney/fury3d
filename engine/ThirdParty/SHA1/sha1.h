#ifndef _FURY_SHA1_H_
#define _FURY_SHA1_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	uint32_t state[5];
	uint64_t bitcount;
	uint8_t buffer[64];
	size_t buffer_size;
} fury_sha1_ctx;

void fury_sha1_init(fury_sha1_ctx *ctx);
void fury_sha1_update(fury_sha1_ctx *ctx, const void *data, size_t len);
void fury_sha1_final(fury_sha1_ctx *ctx, uint8_t out_digest[20]);
void fury_sha1_hex(const uint8_t digest[20], char out_hex[41]);

#ifdef __cplusplus
}
#endif

#endif // _FURY_SHA1_H_
