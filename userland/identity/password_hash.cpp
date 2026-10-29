#include "password_hash.h"
#include "fl/hex.h"
#include "fl/random.h"
extern "C" {
#include "fl/mem_asm.h"
}
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <climits>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

#define FL_PASSWORD_STRETCH_ITERATIONS 60000u
#define FL_PASSWORD_RECORD_PREFIX "pbkdf2-sha256$"

static int hex_decode(const char *hex, std::vector<uint8_t> &out) {
    size_t n = strlen(hex);
    if (n % 2 != 0)
        return -1;
    out.assign(n / 2, 0);
    return fl_hex_decode(hex, out.data(), n / 2);
}

static int pbkdf2_sha256(const char *password, const std::vector<uint8_t> &salt,
                         uint32_t iterations, uint8_t out[32]) {
    if (!password || salt.empty() || iterations == 0)
        return -1;
    return PKCS5_PBKDF2_HMAC(password, (int)strlen(password), salt.data(),
                             (int)salt.size(), (int)iterations, EVP_sha256(), 32, out) == 1
               ? 0
               : -1;
}

static int hash_legacy_bin(const std::vector<uint8_t> &salt, const char *password,
                           uint8_t out[32]) {
    std::vector<uint8_t> material;
    material.reserve(salt.size() + strlen(password));
    material.insert(material.end(), salt.begin(), salt.end());
    material.insert(material.end(), password, password + strlen(password));
    SHA256(material.data(), material.size(), out);
    /* salt || password: wiped before the vector is freed. */
    asm_mem_secure_zero(material.data(), material.size());
    return 0;
}

static int hash_stretched_bin(const std::vector<uint8_t> &salt, const char *password,
                              uint8_t out[32]) {
    uint32_t iter;
    if (hash_legacy_bin(salt, password, out) != 0)
        return -1;
    for (iter = 1; iter < FL_PASSWORD_STRETCH_ITERATIONS; iter++)
        SHA256(out, 32, out);
    return 0;
}

static int parse_pbkdf2_record(const char *hash_hex, uint32_t *out_iter, char *digest_hex,
                               size_t digest_hex_size) {
    const char *p;
    char *end = NULL;
    unsigned long iter;

    if (!hash_hex || strncmp(hash_hex, FL_PASSWORD_RECORD_PREFIX, strlen(FL_PASSWORD_RECORD_PREFIX)) != 0)
        return -1;
    p = hash_hex + strlen(FL_PASSWORD_RECORD_PREFIX);
    iter = strtoul(p, &end, 10);
    if (!end || *end != '$' || iter == 0 || iter > UINT32_MAX || iter > (unsigned long)INT_MAX)
        return -1;
    p = end + 1;
    if (strlen(p) != FL_PASSWORD_HASH_HEX_CHARS)
        return -1;
    if (digest_hex_size < FL_PASSWORD_HASH_HEX_CHARS + 1)
        return -1;
    strncpy(digest_hex, p, digest_hex_size - 1);
    digest_hex[digest_hex_size - 1] = '\0';
    *out_iter = (uint32_t)iter;
    return 0;
}

/* 1 when the stored hex text names exactly the 32 digest bytes in computed. The stored
 * text is public and its format is checked up front (either case of hex is accepted);
 * the compare itself is on raw bytes, constant time (asm_mem_ct_equal), with the decoded
 * copy wiped before returning. */
static int hex_digest_verify(const char *expected_hex, const uint8_t computed[32]) {
    uint8_t expected[32];
    int ok;

    if (fl_hex_decode(expected_hex, expected, sizeof expected) != 0) {
        asm_mem_secure_zero(expected, sizeof expected);
        return 0;
    }
    ok = asm_mem_ct_equal(expected, computed, sizeof expected) != 0;
    asm_mem_secure_zero(expected, sizeof expected);
    return ok;
}

} /* namespace */

extern "C" {

int fl_password_generate_salt_hex(char *salt_hex, size_t salt_hex_size) {
    uint8_t raw[FL_PASSWORD_SALT_BYTES];
    if (!salt_hex || salt_hex_size < FL_PASSWORD_SALT_HEX_CHARS + 1)
        return -1;
    if (fl_random_bytes(raw, sizeof(raw)) != 0)
        return -1;
    fl_hex_encode(raw, sizeof(raw), salt_hex);
    return 0;
}

int fl_password_hash_password(const char *password, const char *salt_hex,
                              char *hash_hex, size_t hash_hex_size) {
    std::vector<uint8_t> salt;
    uint8_t digest[32];
    char digest_hex[FL_PASSWORD_HASH_HEX_CHARS + 1];
    int n;

    if (!password || !salt_hex || !hash_hex || hash_hex_size < FL_PASSWORD_HASH_RECORD_MAX)
        return -1;
    if (strlen(salt_hex) != FL_PASSWORD_SALT_HEX_CHARS)
        return -1;
    if (hex_decode(salt_hex, salt) != 0 || salt.size() != FL_PASSWORD_SALT_BYTES)
        return -1;
    if (pbkdf2_sha256(password, salt, FL_PASSWORD_PBKDF2_ITERATIONS, digest) != 0)
        return -1;
    fl_hex_encode(digest, 32, digest_hex);
    asm_mem_secure_zero(digest, sizeof digest);
    n = snprintf(hash_hex, hash_hex_size, "%s%u$%s", FL_PASSWORD_RECORD_PREFIX,
                 (unsigned)FL_PASSWORD_PBKDF2_ITERATIONS, digest_hex);
    if (n < 0 || (size_t)n >= hash_hex_size)
        return -1;
    return 0;
}

int fl_password_verify(const char *password, const char *salt_hex, const char *hash_hex) {
    std::vector<uint8_t> salt;
    uint8_t computed[32];
    uint8_t stretched[32];
    uint8_t legacy[32];
    char digest_hex[FL_PASSWORD_HASH_HEX_CHARS + 1];
    uint32_t iter = 0;
    int ok = 0;

    if (!password || !salt_hex || !hash_hex)
        return 0;
    if (strlen(salt_hex) != FL_PASSWORD_SALT_HEX_CHARS)
        return 0;
    if (hex_decode(salt_hex, salt) != 0 || salt.size() != FL_PASSWORD_SALT_BYTES)
        return 0;

    if (parse_pbkdf2_record(hash_hex, &iter, digest_hex, sizeof(digest_hex)) == 0) {
        if (pbkdf2_sha256(password, salt, iter, computed) == 0
            && hex_digest_verify(digest_hex, computed))
            ok = 1;
    } else if (strlen(hash_hex) == FL_PASSWORD_HASH_HEX_CHARS) {
        int ok_stretch = 0;
        int ok_legacy = 0;

        /* Both variants are always evaluated: no short-circuit between them. */
        if (hash_stretched_bin(salt, password, stretched) == 0)
            ok_stretch = hex_digest_verify(hash_hex, stretched);
        if (hash_legacy_bin(salt, password, legacy) == 0)
            ok_legacy = hex_digest_verify(hash_hex, legacy);
        ok = (ok_stretch | ok_legacy) ? 1 : 0;
    }
    /* Password-derived verifiers: wiped on every return. */
    asm_mem_secure_zero(computed, sizeof computed);
    asm_mem_secure_zero(stretched, sizeof stretched);
    asm_mem_secure_zero(legacy, sizeof legacy);
    return ok;
}

} /* extern "C" */
