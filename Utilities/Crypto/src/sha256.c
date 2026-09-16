#include "crypto/sha256.h"

#include <string.h>

#include "crypto/crypto.h"

/* Freestanding SHA-256.  The public context is deliberately opaque. */
typedef struct
{
    uint32_t h[8];
    uint64_t bits;
    uint8_t block[64];
    uint32_t used;
} sha_state_t;
static const uint32_t k[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL, 0x59f111f1UL,
    0x923f82a4UL, 0xab1c5ed5UL, 0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
    0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL, 0xe49b69c1UL, 0xefbe4786UL,
    0x0fc19dc6UL, 0x240ca1ccUL, 0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL,
    0x06ca6351UL, 0x14292967UL, 0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL,
    0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL, 0xa2bfe8a1UL, 0xa81a664bUL,
    0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL,
    0x5b9cca4fUL, 0x682e6ff3UL, 0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL};
static uint32_t ror(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32U - n));
}
static uint32_t ld(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}
static void st(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}
static sha_state_t *S(crypto_sha256_context_t *c)
{
    return (sha_state_t *) (void *) c->native.bytes;
}
static void transform(sha_state_t *s, const uint8_t *b)
{
    uint32_t w[64], a, bv, c, d, e, f, g, h, t1, t2;
    size_t i;
    for (i = 0; i < 16; i++)
        w[i] = ld(b + i * 4);
    for (i = 16; i < 64; i++)
    {
        uint32_t x = w[i - 15], y = w[i - 2];
        w[i] = (ror(x, 7) ^ ror(x, 18) ^ (x >> 3)) + w[i - 16] +
               (ror(y, 17) ^ ror(y, 19) ^ (y >> 10)) + w[i - 7];
    }
    a  = s->h[0];
    bv = s->h[1];
    c  = s->h[2];
    d  = s->h[3];
    e  = s->h[4];
    f  = s->h[5];
    g  = s->h[6];
    h  = s->h[7];
    for (i = 0; i < 64; i++)
    {
        uint32_t s1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25), ch = (e & f) ^ ((~e) & g),
                 s0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22), mj = (a & bv) ^ (a & c) ^ (bv & c);
        t1 = h + s1 + ch + k[i] + w[i];
        t2 = s0 + mj;
        h  = g;
        g  = f;
        f  = e;
        e  = d + t1;
        d  = c;
        c  = bv;
        bv = a;
        a  = t1 + t2;
    }
    s->h[0] += a;
    s->h[1] += bv;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
    s->h[5] += f;
    s->h[6] += g;
    s->h[7] += h;
    Crypto_SecureZero(w, sizeof(w));
}
firmware_status_t Crypto_Sha256Init(crypto_sha256_context_t *c)
{
    sha_state_t *s;
    if (c == NULL || sizeof(sha_state_t) > CRYPTO_SHA256_CONTEXT_STORAGE_SIZE)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    memset(c, 0, sizeof(*c));
    s        = S(c);
    s->h[0]  = 0x6a09e667UL;
    s->h[1]  = 0xbb67ae85UL;
    s->h[2]  = 0x3c6ef372UL;
    s->h[3]  = 0xa54ff53aUL;
    s->h[4]  = 0x510e527fUL;
    s->h[5]  = 0x9b05688cUL;
    s->h[6]  = 0x1f83d9abUL;
    s->h[7]  = 0x5be0cd19UL;
    c->state = 0x53484132UL;
    return FIRMWARE_STATUS_OK;
}
firmware_status_t Crypto_Sha256Update(crypto_sha256_context_t *c, const uint8_t *d, size_t n)
{
    sha_state_t *s;
    size_t q;
    if (c == NULL || c->state != 0x53484132UL || (d == NULL && n))
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    s = S(c);
    s->bits += (uint64_t) n * 8ULL;
    while (n)
    {
        q = 64U - s->used;
        if (q > n)
            q = n;
        memcpy(s->block + s->used, d, q);
        s->used += (uint32_t) q;
        d += q;
        n -= q;
        if (s->used == 64U)
        {
            transform(s, s->block);
            s->used = 0U;
        }
    }
    return FIRMWARE_STATUS_OK;
}
firmware_status_t Crypto_Sha256Finish(crypto_sha256_context_t *c, uint8_t out[32])
{
    sha_state_t *s;
    uint64_t bits;
    size_t i;
    if (c == NULL || out == NULL || c->state != 0x53484132UL)
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    s                   = S(c);
    bits                = s->bits;
    s->block[s->used++] = 0x80;
    if (s->used > 56)
    {
        while (s->used < 64)
            s->block[s->used++] = 0;
        transform(s, s->block);
        s->used = 0;
    }
    while (s->used < 56)
        s->block[s->used++] = 0;
    for (i = 0; i < 8; i++)
        s->block[56 + i] = (uint8_t) (bits >> (56 - 8 * i));
    transform(s, s->block);
    for (i = 0; i < 8; i++)
        st(out + i * 4, s->h[i]);
    Crypto_SecureZero(c, sizeof(*c));
    return FIRMWARE_STATUS_OK;
}
void Crypto_Sha256Abort(crypto_sha256_context_t *c)
{
    if (c)
        Crypto_SecureZero(c, sizeof(*c));
}
firmware_status_t Crypto_Sha256(const uint8_t *d, size_t n, uint8_t out[32])
{
    crypto_sha256_context_t c;
    firmware_status_t s = Crypto_Sha256Init(&c);
    if (FirmwareStatus_IsOk(s))
        s = Crypto_Sha256Update(&c, d, n);
    if (FirmwareStatus_IsOk(s))
        return Crypto_Sha256Finish(&c, out);
    Crypto_Sha256Abort(&c);
    return s;
}
