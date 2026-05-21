#include "password_hash.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Sha256Ctx {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    size_t datalen;
};

static const uint32_t k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32u - n)); }

static void sha_transform(Sha256Ctx &ctx, const uint8_t data[64]) {
    uint32_t m[64];
    uint32_t a, b, c, d, e, f, g, h, t1, t2;
    for (int i = 0, j = 0; i < 16; i++, j += 4)
        m[i] = (uint32_t)data[j] << 24 | (uint32_t)data[j + 1] << 16 |
               (uint32_t)data[j + 2] << 8 | (uint32_t)data[j + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
        uint32_t s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }
    a = ctx.state[0];
    b = ctx.state[1];
    c = ctx.state[2];
    d = ctx.state[3];
    e = ctx.state[4];
    f = ctx.state[5];
    g = ctx.state[6];
    h = ctx.state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        t1 = h + S1 + ch + k[i] + m[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        t2 = S0 + maj;
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

static void sha_init(Sha256Ctx &ctx) {
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

static void sha_update(Sha256Ctx &ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        ctx.data[ctx.datalen++] = data[i];
        if (ctx.datalen == 64) {
            sha_transform(ctx, ctx.data);
            ctx.bitlen += 512;
            ctx.datalen = 0;
        }
    }
}

static void sha_final(Sha256Ctx &ctx, uint8_t hash[32]) {
    size_t i = ctx.datalen;
    if (ctx.datalen < 56) {
        ctx.data[i++] = 0x80;
        while (i < 56)
            ctx.data[i++] = 0;
    } else {
        ctx.data[i++] = 0x80;
        while (i < 64)
            ctx.data[i++] = 0;
        sha_transform(ctx, ctx.data);
        i = 0;
        while (i < 56)
            ctx.data[i++] = 0;
    }
    ctx.bitlen += (uint64_t)ctx.datalen * 8u;
    ctx.data[63] = (uint8_t)(ctx.bitlen);
    ctx.data[62] = (uint8_t)(ctx.bitlen >> 8);
    ctx.data[61] = (uint8_t)(ctx.bitlen >> 16);
    ctx.data[60] = (uint8_t)(ctx.bitlen >> 24);
    ctx.data[59] = (uint8_t)(ctx.bitlen >> 32);
    ctx.data[58] = (uint8_t)(ctx.bitlen >> 40);
    ctx.data[57] = (uint8_t)(ctx.bitlen >> 48);
    ctx.data[56] = (uint8_t)(ctx.bitlen >> 56);
    sha_transform(ctx, ctx.data);
    for (i = 0; i < 4; i++) {
        hash[i] = (uint8_t)((ctx.state[0] >> (24 - i * 8)) & 0xff);
        hash[i + 4] = (uint8_t)((ctx.state[1] >> (24 - i * 8)) & 0xff);
        hash[i + 8] = (uint8_t)((ctx.state[2] >> (24 - i * 8)) & 0xff);
        hash[i + 12] = (uint8_t)((ctx.state[3] >> (24 - i * 8)) & 0xff);
        hash[i + 16] = (uint8_t)((ctx.state[4] >> (24 - i * 8)) & 0xff);
        hash[i + 20] = (uint8_t)((ctx.state[5] >> (24 - i * 8)) & 0xff);
        hash[i + 24] = (uint8_t)((ctx.state[6] >> (24 - i * 8)) & 0xff);
        hash[i + 28] = (uint8_t)((ctx.state[7] >> (24 - i * 8)) & 0xff);
    }
}

static std::string sha256_hex(const std::string &input) {
    Sha256Ctx ctx;
    uint8_t hash[32];
    char hex[65];
    sha_init(ctx);
    sha_update(ctx, reinterpret_cast<const uint8_t *>(input.data()), input.size());
    sha_final(ctx, hash);
    for (int i = 0; i < 32; i++)
        snprintf(hex + i * 2, 3, "%02x", hash[i]);
    hex[64] = '\0';
    return std::string(hex);
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int hex_decode(const char *hex, std::vector<uint8_t> &out) {
    size_t n = strlen(hex);
    if (n % 2 != 0)
        return -1;
    out.clear();
    out.reserve(n / 2);
    for (size_t i = 0; i < n; i += 2) {
        int hi = hex_nibble(hex[i]);
        int lo = hex_nibble(hex[i + 1]);
        if (hi < 0 || lo < 0)
            return -1;
        out.push_back((uint8_t)((hi << 4) | lo));
    }
    return 0;
}

} /* namespace */

extern "C" {

int fl_password_generate_salt_hex(char *salt_hex, size_t salt_hex_size) {
    uint8_t raw[FL_PASSWORD_SALT_BYTES];
    FILE *f;
    if (!salt_hex || salt_hex_size < FL_PASSWORD_SALT_HEX_CHARS + 1)
        return -1;
    f = fopen("/dev/urandom", "rb");
    if (!f)
        return -1;
    if (fread(raw, 1, sizeof(raw), f) != sizeof(raw)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    for (size_t i = 0; i < sizeof(raw); i++)
        snprintf(salt_hex + i * 2, 3, "%02x", raw[i]);
    salt_hex[FL_PASSWORD_SALT_HEX_CHARS] = '\0';
    return 0;
}

int fl_password_hash_password(const char *password, const char *salt_hex,
                              char *hash_hex, size_t hash_hex_size) {
    std::vector<uint8_t> salt;
    std::string material;
    std::string digest;
    if (!password || !salt_hex || !hash_hex || hash_hex_size < FL_PASSWORD_HASH_HEX_CHARS + 1)
        return -1;
    if (hex_decode(salt_hex, salt) != 0)
        return -1;
    material.assign(reinterpret_cast<const char *>(salt.data()), salt.size());
    material.append(password);
    digest = sha256_hex(material);
    strncpy(hash_hex, digest.c_str(), hash_hex_size - 1);
    hash_hex[hash_hex_size - 1] = '\0';
    return 0;
}

int fl_password_verify(const char *password, const char *salt_hex, const char *hash_hex) {
    char computed[FL_PASSWORD_HASH_HEX_CHARS + 1];
    size_t i;
    unsigned char diff = 0;
    if (!password || !salt_hex || !hash_hex)
        return 0;
    if (fl_password_hash_password(password, salt_hex, computed, sizeof(computed)) != 0)
        return 0;
    if (strlen(hash_hex) != FL_PASSWORD_HASH_HEX_CHARS || strlen(computed) != FL_PASSWORD_HASH_HEX_CHARS)
        return 0;
    for (i = 0; i < FL_PASSWORD_HASH_HEX_CHARS; i++)
        diff |= (unsigned char)(hash_hex[i] ^ computed[i]);
    return diff == 0;
}

} /* extern "C" */
