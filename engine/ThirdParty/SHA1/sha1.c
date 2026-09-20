#include "sha1.h"

#include <string.h>

static uint32_t sha1_rol(uint32_t v, int bits)
{
	return (v << bits) | (v >> (32 - bits));
}

static void sha1_block(fury_sha1_ctx *ctx, const uint8_t *p)
{
	uint32_t w[80];
	for (int i = 0; i < 16; i++)
		w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | (uint32_t)p[i * 4 + 3];
	for (int i = 16; i < 80; i++)
		w[i] = sha1_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

	uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3], e = ctx->state[4];
	for (int i = 0; i < 80; i++)
	{
		uint32_t f, k;
		if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
		else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
		else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
		else { f = b ^ c ^ d; k = 0xCA62C1D6; }
		uint32_t t = sha1_rol(a, 5) + f + e + k + w[i];
		e = d; d = c; c = sha1_rol(b, 30); b = a; a = t;
	}
	ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d; ctx->state[4] += e;
}

void fury_sha1_init(fury_sha1_ctx *ctx)
{
	ctx->state[0] = 0x67452301;
	ctx->state[1] = 0xEFCDAB89;
	ctx->state[2] = 0x98BADCFE;
	ctx->state[3] = 0x10325476;
	ctx->state[4] = 0xC3D2E1F0;
	ctx->bitcount = 0;
	ctx->buffer_size = 0;
}

void fury_sha1_update(fury_sha1_ctx *ctx, const void *data, size_t len)
{
	const uint8_t *p = (const uint8_t *)data;
	ctx->bitcount += (uint64_t)len * 8;

	if (ctx->buffer_size > 0)
	{
		size_t need = 64 - ctx->buffer_size;
		size_t take = len < need ? len : need;
		memcpy(ctx->buffer + ctx->buffer_size, p, take);
		ctx->buffer_size += take;
		p += take;
		len -= take;
		if (ctx->buffer_size == 64)
		{
			sha1_block(ctx, ctx->buffer);
			ctx->buffer_size = 0;
		}
	}
	while (len >= 64)
	{
		sha1_block(ctx, p);
		p += 64;
		len -= 64;
	}
	if (len > 0)
	{
		memcpy(ctx->buffer, p, len);
		ctx->buffer_size = len;
	}
}

void fury_sha1_final(fury_sha1_ctx *ctx, uint8_t out_digest[20])
{
	uint64_t bits = ctx->bitcount;
	uint8_t pad = 0x80;
	fury_sha1_update(ctx, &pad, 1);
	uint8_t zero = 0;
	while (ctx->buffer_size != 56)
		fury_sha1_update(ctx, &zero, 1);
	uint8_t len_bytes[8];
	for (int i = 0; i < 8; i++)
		len_bytes[i] = (uint8_t)(bits >> (56 - i * 8));
	/* feed length without touching bitcount */
	if (ctx->buffer_size + 8 <= 64)
	{
		memcpy(ctx->buffer + ctx->buffer_size, len_bytes, 8);
		sha1_block(ctx, ctx->buffer);
	}
	for (int i = 0; i < 5; i++)
	{
		out_digest[i * 4] = (uint8_t)(ctx->state[i] >> 24);
		out_digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
		out_digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
		out_digest[i * 4 + 3] = (uint8_t)(ctx->state[i]);
	}
}

void fury_sha1_hex(const uint8_t digest[20], char out_hex[41])
{
	static const char *digits = "0123456789abcdef";
	for (int i = 0; i < 20; i++)
	{
		out_hex[i * 2] = digits[digest[i] >> 4];
		out_hex[i * 2 + 1] = digits[digest[i] & 0x0F];
	}
	out_hex[40] = '\0';
}
