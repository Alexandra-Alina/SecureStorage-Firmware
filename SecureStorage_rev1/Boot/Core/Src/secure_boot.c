/**
 * secure_boot.c — Minimal HMAC-SHA256 firmware integrity verification.
 *
 * Self-contained: implements SHA-256 and HMAC from scratch (RFC 6234 /
 * FIPS 180-4) with no external library dependencies beyond <stdint.h>
 * and <string.h>, both available via CMSIS in the Boot project.
 *
 * The Boot project embeds a 32-byte secret HMAC key (boot_hmac_tag.h).
 * At boot time this module reads the Appli binary from NOR flash
 * (memory-mapped by ExtMem after MX_EXTMEM_MANAGER_Init), recomputes
 * HMAC-SHA256, and compares it in constant time against the expected
 * tag also stored in Boot flash.
 *
 * When RDP Level-1 is enabled the Boot's internal flash (and therefore
 * both the key and the tag) cannot be read via JTAG/SWD, making
 * forgery infeasible without the signing key.
 */

#include <stdint.h>
#include <string.h>
#include "secure_boot.h"
#include "boot_hmac_tag.h"

/* ── SHA-256 (FIPS 180-4) ─────────────────────────────────────────────── */

#define ROR32(x, n)  (((x) >> (n)) | ((x) << (32u - (n))))
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)  (ROR32(x,  2) ^ ROR32(x, 13) ^ ROR32(x, 22))
#define EP1(x)  (ROR32(x,  6) ^ ROR32(x, 11) ^ ROR32(x, 25))
#define SIG0(x) (ROR32(x,  7) ^ ROR32(x, 18) ^ ((x) >>  3))
#define SIG1(x) (ROR32(x, 17) ^ ROR32(x, 19) ^ ((x) >> 10))

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

typedef struct {
    uint8_t  buf[64];
    uint32_t state[8];
    uint64_t bits;
    uint32_t len;
} sha256_ctx_t;

static void sha256_compress(sha256_ctx_t *ctx, const uint8_t *blk)
{
    uint32_t a, b, c, d, e, f, g, h, t1, t2, w[64];
    int i;

    for (i = 0; i < 16; i++, blk += 4)
        w[i] = ((uint32_t)blk[0] << 24) | ((uint32_t)blk[1] << 16)
             | ((uint32_t)blk[2] <<  8) |  (uint32_t)blk[3];
    for (i = 16; i < 64; i++)
        w[i] = SIG1(w[i-2]) + w[i-7] + SIG0(w[i-15]) + w[i-16];

    a = ctx->state[0]; b = ctx->state[1];
    c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5];
    g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e, f, g) + K[i] + w[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_ctx_t *ctx)
{
    ctx->bits = 0; ctx->len = 0;
    ctx->state[0] = 0x6a09e667u; ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u; ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu; ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu; ctx->state[7] = 0x5be0cd19u;
}

static void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        ctx->buf[ctx->len++] = data[i];
        if (ctx->len == 64u) {
            sha256_compress(ctx, ctx->buf);
            ctx->bits += 512u;
            ctx->len = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t out[32])
{
    uint32_t i = ctx->len;
    ctx->buf[i++] = 0x80u;
    if (ctx->len < 56u) {
        while (i < 56u) ctx->buf[i++] = 0;
    } else {
        while (i < 64u) ctx->buf[i++] = 0;
        sha256_compress(ctx, ctx->buf);
        memset(ctx->buf, 0, 56);
    }
    ctx->bits += (uint64_t)ctx->len * 8u;
    for (i = 0; i < 8u; i++) { ctx->buf[63 - i] = (uint8_t)(ctx->bits >> (i * 8u)); }
    sha256_compress(ctx, ctx->buf);
    for (i = 0; i < 4u; i++) {
        out[i]      = (uint8_t)(ctx->state[0] >> (24u - i * 8u));
        out[i +  4] = (uint8_t)(ctx->state[1] >> (24u - i * 8u));
        out[i +  8] = (uint8_t)(ctx->state[2] >> (24u - i * 8u));
        out[i + 12] = (uint8_t)(ctx->state[3] >> (24u - i * 8u));
        out[i + 16] = (uint8_t)(ctx->state[4] >> (24u - i * 8u));
        out[i + 20] = (uint8_t)(ctx->state[5] >> (24u - i * 8u));
        out[i + 24] = (uint8_t)(ctx->state[6] >> (24u - i * 8u));
        out[i + 28] = (uint8_t)(ctx->state[7] >> (24u - i * 8u));
    }
}

/* ── HMAC-SHA256 (RFC 2104) ───────────────────────────────────────────── */

static void hmac_sha256(const uint8_t *key,  uint32_t klen,
                        const uint8_t *msg,  uint32_t mlen,
                        uint8_t        mac[32])
{
    uint8_t k_ipad[64], k_opad[64], inner[32];
    sha256_ctx_t ctx;
    uint32_t i;

    memset(k_ipad, 0, 64);
    memset(k_opad, 0, 64);
    memcpy(k_ipad, key, klen > 64u ? 64u : klen);
    memcpy(k_opad, key, klen > 64u ? 64u : klen);

    for (i = 0; i < 64u; i++) { k_ipad[i] ^= 0x36u; k_opad[i] ^= 0x5cu; }

    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, 64);
    sha256_update(&ctx, msg,    mlen);
    sha256_final(&ctx, inner);

    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, 64);
    sha256_update(&ctx, inner,  32);
    sha256_final(&ctx, mac);
}

/* ── Public API ───────────────────────────────────────────────────────── */

int secure_boot_verify(void)
{
    const uint8_t *appli = (const uint8_t *)APPLI_BASE_ADDR;
    uint8_t computed[32];

    hmac_sha256(HMAC_KEY, 32u, appli, (uint32_t)APPLI_SIZE, computed);

    /* Constant-time comparison — prevents early-exit timing side-channels */
    volatile uint8_t diff = 0;
    for (uint32_t i = 0; i < 32u; i++) diff |= computed[i] ^ EXPECTED_TAG[i];

    return (diff == 0u) ? SECURE_BOOT_OK : SECURE_BOOT_FAIL;
}
