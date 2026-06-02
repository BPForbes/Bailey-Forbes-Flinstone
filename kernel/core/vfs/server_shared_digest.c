#include "server_shared_digest.h"
#include "contract_p5_server_catalog.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <string.h>

fl_result_t fl_server_shared_sha256_hex(const uint8_t *data,
                                        size_t len,
                                        char *out,
                                        size_t out_cap)
{
    unsigned char digest[32];
    unsigned int digest_len = 0u;
    EVP_MD_CTX *ctx;
    size_t i;

    if (!out || out_cap < FL_SERVER_CATALOG_HASH_HEX_MAX)
        return FL_RESULT_INVAL;
    if (len > 0u && !data)
        return FL_RESULT_INVAL;

    ctx = EVP_MD_CTX_new();
    if (!ctx)
        return FL_RESULT_ERR;
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1 ||
        (len > 0u && EVP_DigestUpdate(ctx, data, len) != 1) ||
        EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1 ||
        digest_len != 32u) {
        EVP_MD_CTX_free(ctx);
        return FL_RESULT_ERR;
    }
    EVP_MD_CTX_free(ctx);

    for (i = 0; i < 32u; i++)
        snprintf(out + (i * 2u), out_cap - (i * 2u), "%02x", (unsigned)digest[i]);
    out[64] = '\0';
    return FL_RESULT_OK;
}

fl_result_t fl_server_shared_uuid_v4_lower(char *out, size_t out_cap)
{
    unsigned char raw[16];
    int n;

    if (!out || out_cap < 37u)
        return FL_RESULT_INVAL;
    if (RAND_bytes(raw, (int)sizeof(raw)) != 1)
        return FL_RESULT_ERR;
    raw[6] = (uint8_t)((raw[6] & 0x0fu) | 0x40u);
    raw[8] = (uint8_t)((raw[8] & 0x3fu) | 0x80u);
    n = snprintf(out, out_cap,
                 "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7], raw[8], raw[9],
                 raw[10], raw[11], raw[12], raw[13], raw[14], raw[15]);
    if (n < 0 || (size_t)n >= out_cap)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}
