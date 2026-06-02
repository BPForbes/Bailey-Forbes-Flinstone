#include "server_shared_digest.h"
#include "contract_p5_server_catalog.h"

#include <openssl/evp.h>
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
