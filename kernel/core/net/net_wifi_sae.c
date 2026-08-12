#include "net_wifi_sae.h"

#include "contract_p3_wifi.h"
#include "fl/mem_asm.h"
#include "net_endian.h"
#include "net_wifi_crypto.h"

#include <stdlib.h>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <string.h>

#define SAE_PRIME_LEN 32u
#define SAE_ORDER_LEN 32u
#define SAE_SCALAR_WIRE_LEN SAE_PRIME_LEN
#define SAE_ELEMENT_WIRE_LEN (2u * SAE_PRIME_LEN)
#define SAE_KCK_LEN   32u
#define SAE_PMK_LEN   32u
#define SAE_CONFIRM_LEN 32u
#define SAE_PWE_MAX_LOOP 40u

struct fl_net_wifi_sae_dragonfly_ctx {
    int is_ap;
    char ssid[FL_WIFI_SSID_MAX + 1u];
    char password[FL_WIFI_PASSPHRASE_MAX + 1u];
    uint8_t own_addr[6];
    uint8_t peer_addr[6];

    EC_GROUP *group;
    BIGNUM *order;
    BIGNUM *prime;
    BIGNUM *a;
    BIGNUM *b;
    EC_POINT *pwe;

    BIGNUM *own_rand;
    BIGNUM *own_mask;
    BIGNUM *own_scalar;
    EC_POINT *own_element;
    BIGNUM *peer_scalar;
    EC_POINT *peer_element;

    uint8_t kck[SAE_KCK_LEN];
    uint8_t pmk[SAE_PMK_LEN];
    uint16_t send_confirm;
    int peer_commit_seen;
};

static int sae_mac_cmp(const uint8_t a[6], const uint8_t b[6])
{
    return memcmp(a, b, 6);
}

static fl_result_t sae_bn_to_bin_be(const BIGNUM *bn, uint8_t *out, size_t out_len)
{
    int need;

    if (!bn || !out || out_len == 0u)
        return FL_RESULT_INVAL;
    need = BN_num_bytes(bn);
    if (need < 0 || (size_t)need > out_len)
        return FL_RESULT_ERR;
    asm_mem_zero(out, out_len);
    if (BN_bn2bin(bn, out + (out_len - (size_t)need)) < 0)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}

static fl_result_t sae_bin_to_bn_be(BIGNUM *dst, const uint8_t *in, size_t in_len)
{
    if (!dst || !in || in_len == 0u)
        return FL_RESULT_INVAL;
    if (!BN_bin2bn(in, (int)in_len, dst))
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}

static fl_result_t sae_kdf_hash(const uint8_t *key, size_t key_len, const char *label,
                                const uint8_t *context, size_t context_len, uint8_t *out,
                                size_t out_len)
{
    return fl_net_wifi_crypto_sae_kdf(key, key_len, label, context, context_len, out, out_len);
}

static fl_result_t sae_hkdf_extract(const uint8_t *salt, size_t salt_len, const uint8_t *ikm,
                                    size_t ikm_len, uint8_t *prk_out, size_t prk_len)
{
    unsigned int mac_len = 0u;

    if (!salt || !ikm || !prk_out || prk_len == 0u)
        return FL_RESULT_INVAL;
    if (HMAC(EVP_sha256(), salt, (int)salt_len, ikm, ikm_len, prk_out, &mac_len) == NULL ||
        mac_len != prk_len)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}

static fl_result_t sae_derive_keys_from_k(fl_net_wifi_sae_dragonfly_ctx_t *ctx, const uint8_t *k_x,
                                          size_t k_len)
{
    uint8_t zero_salt[32];
    uint8_t keyseed[32];
    uint8_t scalar_sum_buf[SAE_ORDER_LEN];
    uint8_t keys[SAE_KCK_LEN + SAE_PMK_LEN];
    BIGNUM *sum = NULL;
    BN_CTX *bnctx = NULL;
    fl_result_t rc = FL_RESULT_ERR;

    if (!ctx || !k_x || k_len != SAE_PRIME_LEN)
        return FL_RESULT_INVAL;

    asm_mem_zero(zero_salt, sizeof(zero_salt));
    if (sae_hkdf_extract(zero_salt, sizeof(zero_salt), k_x, k_len, keyseed, sizeof(keyseed)) !=
        FL_RESULT_OK)
        return FL_RESULT_ERR;

    bnctx = BN_CTX_new();
    sum = BN_new();
    if (!bnctx || !sum)
        goto done;

    if (!BN_mod_add(sum, ctx->own_scalar, ctx->peer_scalar, ctx->order, bnctx))
        goto done;
    if (sae_bn_to_bin_be(sum, scalar_sum_buf, SAE_ORDER_LEN) != FL_RESULT_OK)
        goto done;

    if (sae_kdf_hash(keyseed, sizeof(keyseed), "SAE KCK and PMK", scalar_sum_buf,
                     SAE_ORDER_LEN, keys, sizeof(keys)) != FL_RESULT_OK)
        goto done;
    asm_mem_copy(ctx->kck, keys, SAE_KCK_LEN);
    asm_mem_copy(ctx->pmk, keys + SAE_KCK_LEN, SAE_PMK_LEN);
    rc = FL_RESULT_OK;

done:
    fl_net_wifi_crypto_memzero(zero_salt, sizeof(zero_salt));
    fl_net_wifi_crypto_memzero(keyseed, sizeof(keyseed));
    fl_net_wifi_crypto_memzero(scalar_sum_buf, sizeof(scalar_sum_buf));
    fl_net_wifi_crypto_memzero(keys, sizeof(keys));
    BN_clear_free(sum);
    BN_CTX_free(bnctx);
    return rc;
}

static fl_result_t sae_derive_pwe_ecc(fl_net_wifi_sae_dragonfly_ctx_t *ctx)
{
    uint8_t addr1[6];
    uint8_t addr2[6];
    uint8_t pwd_seed[SHA256_DIGEST_LENGTH];
    uint8_t pwd_value_buf[SAE_PRIME_LEN];
    uint8_t counter = 0u;
    size_t pass_len;
    size_t ssid_len;
    EVP_MD_CTX *md = NULL;
    unsigned int md_len = 0u;
    BIGNUM *x = NULL;
    BIGNUM *y2 = NULL;
    BIGNUM *y = NULL;
    BN_CTX *bnctx = NULL;
    fl_result_t rc = FL_RESULT_ERR;

    if (!ctx || !ctx->group || !ctx->prime || !ctx->a || !ctx->b || !ctx->pwe)
        return FL_RESULT_INVAL;

    pass_len = strlen(ctx->password);
    ssid_len = strlen(ctx->ssid);
    if (pass_len < 1u || ssid_len < 1u)
        return FL_RESULT_INVAL;

    if (sae_mac_cmp(ctx->own_addr, ctx->peer_addr) > 0) {
        asm_mem_copy(addr1, ctx->own_addr, 6u);
        asm_mem_copy(addr2, ctx->peer_addr, 6u);
    } else {
        asm_mem_copy(addr1, ctx->peer_addr, 6u);
        asm_mem_copy(addr2, ctx->own_addr, 6u);
    }

    bnctx = BN_CTX_new();
    x = BN_new();
    y2 = BN_new();
    y = BN_new();
    if (!bnctx || !x || !y2 || !y)
        goto done;

    for (counter = 0u; counter < SAE_PWE_MAX_LOOP; counter++) {
        md = EVP_MD_CTX_new();
        if (!md)
            goto done;
        if (EVP_DigestInit_ex(md, EVP_sha256(), NULL) != 1 ||
            EVP_DigestUpdate(md, addr1, 6u) != 1 ||
            EVP_DigestUpdate(md, addr2, 6u) != 1 ||
            EVP_DigestUpdate(md, ctx->password, pass_len) != 1 ||
            EVP_DigestUpdate(md, &counter, 1u) != 1 ||
            EVP_DigestFinal_ex(md, pwd_seed, &md_len) != 1) {
            EVP_MD_CTX_free(md);
            md = NULL;
            goto done;
        }
        EVP_MD_CTX_free(md);
        md = NULL;

        if (sae_kdf_hash(pwd_seed, sizeof(pwd_seed), "saepw", (const uint8_t *)ctx->ssid,
                         ssid_len, pwd_value_buf, sizeof(pwd_value_buf)) != FL_RESULT_OK)
            goto done;

        if (sae_bin_to_bn_be(x, pwd_value_buf, sizeof(pwd_value_buf)) != FL_RESULT_OK)
            goto done;
        if (BN_ucmp(x, ctx->prime) >= 0)
            continue;

        if (!BN_mod_sqr(y2, x, ctx->prime, bnctx))
            goto done;
        if (!BN_mod_add(y2, y2, ctx->a, ctx->prime, bnctx))
            goto done;
        if (!BN_mod_mul(y2, y2, x, ctx->prime, bnctx))
            goto done;
        if (!BN_mod_add(y2, y2, ctx->b, ctx->prime, bnctx))
            goto done;

        if (BN_mod_sqrt(y, y2, ctx->prime, bnctx) == NULL)
            continue;

        if ((BN_is_odd(x) != 0) != (BN_is_odd(y) != 0)) {
            if (!BN_sub(y, ctx->prime, y))
                goto done;
        }

        if (!EC_POINT_set_affine_coordinates(ctx->group, ctx->pwe, x, y, bnctx))
            continue;
        if (EC_POINT_is_on_curve(ctx->group, ctx->pwe, bnctx) != 1)
            continue;
        rc = FL_RESULT_OK;
        goto done;
    }

done:
    if (md)
        EVP_MD_CTX_free(md);
    BN_clear_free(x);
    BN_clear_free(y2);
    BN_clear_free(y);
    BN_CTX_free(bnctx);
    fl_net_wifi_crypto_memzero(pwd_seed, sizeof(pwd_seed));
    fl_net_wifi_crypto_memzero(pwd_value_buf, sizeof(pwd_value_buf));
    return rc;
}

static fl_result_t sae_prepare_commit(fl_net_wifi_sae_dragonfly_ctx_t *ctx)
{
    uint8_t rand_buf[SAE_ORDER_LEN];
    uint8_t mask_buf[SAE_ORDER_LEN];
    BIGNUM *tmp = NULL;
    EC_POINT *masked = NULL;
    BN_CTX *bnctx = NULL;
    fl_result_t rc = FL_RESULT_ERR;

    if (!ctx || !ctx->own_rand || !ctx->own_mask || !ctx->own_scalar || !ctx->own_element ||
        !ctx->pwe)
        return FL_RESULT_INVAL;

    bnctx = BN_CTX_new();
    tmp = BN_new();
    masked = EC_POINT_new(ctx->group);
    if (!bnctx || !tmp || !masked)
        goto done;

    if (fl_net_wifi_crypto_random(rand_buf, sizeof(rand_buf)) != FL_RESULT_OK ||
        fl_net_wifi_crypto_random(mask_buf, sizeof(mask_buf)) != FL_RESULT_OK)
        goto done;

    if (sae_bin_to_bn_be(ctx->own_rand, rand_buf, sizeof(rand_buf)) != FL_RESULT_OK ||
        sae_bin_to_bn_be(ctx->own_mask, mask_buf, sizeof(mask_buf)) != FL_RESULT_OK)
        goto done;

    BN_mod(ctx->own_rand, ctx->own_rand, ctx->order, bnctx);
    BN_mod(ctx->own_mask, ctx->own_mask, ctx->order, bnctx);
    if (BN_is_zero(ctx->own_rand) || BN_is_zero(ctx->own_mask))
        goto done;

    if (!BN_mod_add(ctx->own_scalar, ctx->own_rand, ctx->own_mask, ctx->order, bnctx))
        goto done;

    if (!EC_POINT_mul(ctx->group, masked, NULL, ctx->pwe, ctx->own_mask, bnctx))
        goto done;
    if (!EC_POINT_invert(ctx->group, masked, bnctx))
        goto done;
    if (!EC_POINT_copy(ctx->own_element, masked))
        goto done;

    rc = FL_RESULT_OK;

done:
    fl_net_wifi_crypto_memzero(rand_buf, sizeof(rand_buf));
    fl_net_wifi_crypto_memzero(mask_buf, sizeof(mask_buf));
    BN_clear_free(tmp);
    EC_POINT_clear_free(masked);
    BN_CTX_free(bnctx);
    return rc;
}

static fl_result_t sae_write_commit_body(fl_net_wifi_sae_dragonfly_ctx_t *ctx,
                                         const uint8_t *anticlogging_token,
                                         size_t anticlogging_len, uint8_t *body, size_t body_cap,
                                         size_t *body_len_out)
{
    uint8_t scalar_be[SAE_SCALAR_WIRE_LEN];
    uint8_t element_be[SAE_ELEMENT_WIRE_LEN];
    BIGNUM *x = NULL;
    BIGNUM *y = NULL;
    BN_CTX *bnctx = NULL;
    size_t need;
    fl_result_t rc = FL_RESULT_ERR;

    if (!ctx || !body || !body_len_out)
        return FL_RESULT_INVAL;

    need = 2u + SAE_SCALAR_WIRE_LEN + SAE_ELEMENT_WIRE_LEN + anticlogging_len;
    if (body_cap < need)
        return FL_RESULT_INVAL;

    bnctx = BN_CTX_new();
    x = BN_new();
    y = BN_new();
    if (!bnctx || !x || !y)
        goto done;

    fl_net_put_u16_le(body, (uint16_t)FL_NET_WIFI_SAE_GROUP_19);

    if (sae_bn_to_bin_be(ctx->own_scalar, scalar_be, SAE_SCALAR_WIRE_LEN) != FL_RESULT_OK)
        goto done;
    asm_mem_copy(body + 2u, scalar_be, SAE_SCALAR_WIRE_LEN);

    if (!EC_POINT_get_affine_coordinates(ctx->group, ctx->own_element, x, y, bnctx))
        goto done;
    if (sae_bn_to_bin_be(x, element_be, SAE_PRIME_LEN) != FL_RESULT_OK ||
        sae_bn_to_bin_be(y, element_be + SAE_PRIME_LEN, SAE_PRIME_LEN) != FL_RESULT_OK)
        goto done;
    asm_mem_copy(body + 2u + SAE_SCALAR_WIRE_LEN, element_be, SAE_ELEMENT_WIRE_LEN);

    if (anticlogging_len > 0u) {
        if (!anticlogging_token)
            goto done;
        asm_mem_copy(body + 2u + SAE_SCALAR_WIRE_LEN + SAE_ELEMENT_WIRE_LEN, anticlogging_token,
               anticlogging_len);
    }

    *body_len_out = need;
    rc = FL_RESULT_OK;

done:
    fl_net_wifi_crypto_memzero(scalar_be, sizeof(scalar_be));
    fl_net_wifi_crypto_memzero(element_be, sizeof(element_be));
    BN_clear_free(x);
    BN_clear_free(y);
    BN_CTX_free(bnctx);
    return rc;
}

static fl_result_t sae_parse_commit_body(fl_net_wifi_sae_dragonfly_ctx_t *ctx, const uint8_t *body,
                                         size_t body_len, size_t *commit_len_out)
{
    uint16_t group;
    const uint8_t *scalar_be;
    const uint8_t *element_be;
    size_t min_len = 2u + SAE_SCALAR_WIRE_LEN + SAE_ELEMENT_WIRE_LEN;

    if (!ctx || !body || body_len < min_len)
        return FL_RESULT_INVAL;

    group = fl_net_get_u16_le(body);
    if (group != FL_NET_WIFI_SAE_GROUP_19)
        return FL_RESULT_ERR;

    scalar_be = body + 2u;
    element_be = body + 2u + SAE_SCALAR_WIRE_LEN;

    BN_clear(ctx->peer_scalar);
    if (sae_bin_to_bn_be(ctx->peer_scalar, scalar_be, SAE_SCALAR_WIRE_LEN) != FL_RESULT_OK)
        return FL_RESULT_ERR;

    {
        BIGNUM *x = BN_new();
        BIGNUM *y = BN_new();
        BN_CTX *bnctx = BN_CTX_new();

        if (!bnctx || !x || !y) {
            BN_clear_free(x);
            BN_clear_free(y);
            BN_CTX_free(bnctx);
            return FL_RESULT_ERR;
        }
        if (sae_bin_to_bn_be(x, element_be, SAE_PRIME_LEN) != FL_RESULT_OK ||
            sae_bin_to_bn_be(y, element_be + SAE_PRIME_LEN, SAE_PRIME_LEN) != FL_RESULT_OK ||
            !EC_POINT_set_affine_coordinates(ctx->group, ctx->peer_element, x, y, bnctx)) {
            BN_clear_free(x);
            BN_clear_free(y);
            BN_CTX_free(bnctx);
            return FL_RESULT_ERR;
        }
        BN_clear_free(x);
        BN_clear_free(y);
        BN_CTX_free(bnctx);
    }

    if (commit_len_out)
        *commit_len_out = min_len;
    ctx->peer_commit_seen = 1;
    return FL_RESULT_OK;
}

static fl_result_t sae_derive_k(fl_net_wifi_sae_dragonfly_ctx_t *ctx, uint8_t k_x[SAE_PRIME_LEN])
{
    EC_POINT *k = NULL;
    BIGNUM *k_x_bn = NULL;
    BN_CTX *bnctx = NULL;
    fl_result_t rc = FL_RESULT_ERR;

    if (!ctx || !ctx->peer_commit_seen || !k_x)
        return FL_RESULT_INVAL;

    bnctx = BN_CTX_new();
    k = EC_POINT_new(ctx->group);
    k_x_bn = BN_new();
    if (!bnctx || !k || !k_x_bn)
        goto done;

    if (!EC_POINT_mul(ctx->group, k, NULL, ctx->pwe, ctx->peer_scalar, bnctx))
        goto done;
    if (!EC_POINT_add(ctx->group, k, ctx->peer_element, k, bnctx))
        goto done;
    if (!EC_POINT_mul(ctx->group, k, NULL, k, ctx->own_rand, bnctx))
        goto done;
    if (EC_POINT_is_at_infinity(ctx->group, k))
        goto done;
    if (!EC_POINT_get_affine_coordinates(ctx->group, k, k_x_bn, NULL, bnctx))
        goto done;
    if (sae_bn_to_bin_be(k_x_bn, k_x, SAE_PRIME_LEN) != FL_RESULT_OK)
        goto done;
    if (sae_derive_keys_from_k(ctx, k_x, SAE_PRIME_LEN) != FL_RESULT_OK)
        goto done;
    rc = FL_RESULT_OK;

done:
    EC_POINT_clear_free(k);
    BN_clear_free(k_x_bn);
    BN_CTX_free(bnctx);
    return rc;
}

static fl_result_t sae_point_to_bin_xy(const EC_GROUP *group, const EC_POINT *pt, uint8_t *out,
                                         size_t out_len)
{
    BIGNUM *x = BN_new();
    BIGNUM *y = BN_new();
    BN_CTX *bnctx = BN_CTX_new();
    fl_result_t rc = FL_RESULT_ERR;

    if (!group || !pt || !out || out_len < SAE_ELEMENT_WIRE_LEN || !x || !y || !bnctx)
        goto done;
    if (!EC_POINT_get_affine_coordinates(group, pt, x, y, bnctx))
        goto done;
    if (sae_bn_to_bin_be(x, out, SAE_PRIME_LEN) != FL_RESULT_OK ||
        sae_bn_to_bin_be(y, out + SAE_PRIME_LEN, SAE_PRIME_LEN) != FL_RESULT_OK)
        goto done;
    rc = FL_RESULT_OK;

done:
    BN_clear_free(x);
    BN_clear_free(y);
    BN_CTX_free(bnctx);
    return rc;
}

static fl_result_t sae_build_confirm_value_for_peer(const fl_net_wifi_sae_dragonfly_ctx_t *ctx,
                                                    uint16_t send_confirm,
                                                    const uint8_t scalar_self[SAE_SCALAR_WIRE_LEN],
                                                    const uint8_t scalar_peer[SAE_SCALAR_WIRE_LEN],
                                                    const uint8_t element_self[SAE_ELEMENT_WIRE_LEN],
                                                    const uint8_t element_peer[SAE_ELEMENT_WIRE_LEN],
                                                    uint8_t confirm_out[SAE_CONFIRM_LEN])
{
    uint8_t buf[2u + 2u * SAE_SCALAR_WIRE_LEN + 2u * SAE_ELEMENT_WIRE_LEN];
    unsigned int mac_len = 0u;
    size_t pos = 0u;

    fl_net_put_u16_le(buf, send_confirm);
    pos = 2u;
    asm_mem_copy(buf + pos, scalar_self, SAE_SCALAR_WIRE_LEN);
    pos += SAE_SCALAR_WIRE_LEN;
    asm_mem_copy(buf + pos, element_self, SAE_ELEMENT_WIRE_LEN);
    pos += SAE_ELEMENT_WIRE_LEN;
    asm_mem_copy(buf + pos, scalar_peer, SAE_SCALAR_WIRE_LEN);
    pos += SAE_SCALAR_WIRE_LEN;
    asm_mem_copy(buf + pos, element_peer, SAE_ELEMENT_WIRE_LEN);
    pos += SAE_ELEMENT_WIRE_LEN;

    if (HMAC(EVP_sha256(), ctx->kck, (int)sizeof(ctx->kck), buf, pos, confirm_out, &mac_len) ==
            NULL ||
        mac_len != SAE_CONFIRM_LEN)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}

static fl_result_t sae_build_confirm_value(fl_net_wifi_sae_dragonfly_ctx_t *ctx,
                                           uint8_t confirm_out[SAE_CONFIRM_LEN])
{
    uint8_t scalar_own[SAE_SCALAR_WIRE_LEN];
    uint8_t scalar_peer[SAE_SCALAR_WIRE_LEN];
    uint8_t element_own[SAE_ELEMENT_WIRE_LEN];
    uint8_t element_peer[SAE_ELEMENT_WIRE_LEN];
    fl_result_t rc = FL_RESULT_ERR;

    if (!ctx || !confirm_out || !ctx->peer_commit_seen)
        return FL_RESULT_INVAL;

    if (sae_bn_to_bin_be(ctx->own_scalar, scalar_own, sizeof(scalar_own)) != FL_RESULT_OK ||
        sae_bn_to_bin_be(ctx->peer_scalar, scalar_peer, sizeof(scalar_peer)) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (sae_point_to_bin_xy(ctx->group, ctx->own_element, element_own, sizeof(element_own)) !=
            FL_RESULT_OK ||
        sae_point_to_bin_xy(ctx->group, ctx->peer_element, element_peer, sizeof(element_peer)) !=
            FL_RESULT_OK)
        return FL_RESULT_ERR;

    rc = sae_build_confirm_value_for_peer(ctx, ctx->send_confirm, scalar_own, scalar_peer,
                                          element_own, element_peer, confirm_out);

    fl_net_wifi_crypto_memzero(scalar_own, sizeof(scalar_own));
    fl_net_wifi_crypto_memzero(scalar_peer, sizeof(scalar_peer));
    fl_net_wifi_crypto_memzero(element_own, sizeof(element_own));
    fl_net_wifi_crypto_memzero(element_peer, sizeof(element_peer));
    return rc;
}

fl_result_t fl_net_wifi_sae_dragonfly_ctx_create(fl_net_wifi_sae_dragonfly_ctx_t **ctx_out)
{
    fl_net_wifi_sae_dragonfly_ctx_t *ctx;

    if (!ctx_out)
        return FL_RESULT_INVAL;

    ctx = (fl_net_wifi_sae_dragonfly_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx)
        return FL_RESULT_ERR;

    ctx->group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
    if (!ctx->group) {
        free(ctx);
        return FL_RESULT_ERR;
    }

    ctx->order = BN_new();
    ctx->prime = BN_new();
    ctx->a = BN_new();
    ctx->b = BN_new();
    ctx->pwe = EC_POINT_new(ctx->group);
    ctx->own_rand = BN_new();
    ctx->own_mask = BN_new();
    ctx->own_scalar = BN_new();
    ctx->own_element = EC_POINT_new(ctx->group);
    ctx->peer_scalar = BN_new();
    ctx->peer_element = EC_POINT_new(ctx->group);

    if (!ctx->order || !ctx->prime || !ctx->a || !ctx->b || !ctx->pwe || !ctx->own_rand ||
        !ctx->own_mask || !ctx->own_scalar || !ctx->own_element || !ctx->peer_scalar ||
        !ctx->peer_element) {
        fl_net_wifi_sae_dragonfly_ctx_destroy(ctx);
        return FL_RESULT_ERR;
    }

    if (!EC_GROUP_get_order(ctx->group, ctx->order, NULL) ||
        !EC_GROUP_get_curve(ctx->group, ctx->prime, ctx->a, ctx->b, NULL)) {
        fl_net_wifi_sae_dragonfly_ctx_destroy(ctx);
        return FL_RESULT_ERR;
    }

    ctx->send_confirm = 0u;
    *ctx_out = ctx;
    return FL_RESULT_OK;
}

void fl_net_wifi_sae_dragonfly_ctx_destroy(fl_net_wifi_sae_dragonfly_ctx_t *ctx)
{
    if (!ctx)
        return;
    EC_GROUP_free(ctx->group);
    BN_clear_free(ctx->order);
    BN_clear_free(ctx->prime);
    BN_clear_free(ctx->a);
    BN_clear_free(ctx->b);
    EC_POINT_clear_free(ctx->pwe);
    BN_clear_free(ctx->own_rand);
    BN_clear_free(ctx->own_mask);
    BN_clear_free(ctx->own_scalar);
    EC_POINT_clear_free(ctx->own_element);
    BN_clear_free(ctx->peer_scalar);
    EC_POINT_clear_free(ctx->peer_element);
    fl_net_wifi_crypto_memzero(ctx->kck, sizeof(ctx->kck));
    fl_net_wifi_crypto_memzero(ctx->pmk, sizeof(ctx->pmk));
    fl_net_wifi_crypto_memzero(ctx->password, sizeof(ctx->password));
    free(ctx);
}

static fl_result_t sae_dragonfly_init_common(fl_net_wifi_sae_dragonfly_ctx_t *ctx, int is_ap,
                                           const char *ssid, const char *password,
                                           const uint8_t own_addr[6], const uint8_t peer_addr[6])
{
    size_t ssid_len;
    size_t pass_len;

    if (!ctx || !ssid || !password || !own_addr || !peer_addr)
        return FL_RESULT_INVAL;

    ssid_len = strlen(ssid);
    pass_len = strlen(password);
    if (ssid_len < 1u || ssid_len > FL_WIFI_SSID_MAX)
        return FL_RESULT_INVAL;
    if (pass_len < 1u || pass_len >= FL_WIFI_PASSPHRASE_MAX)
        return FL_RESULT_INVAL;

    ctx->is_ap = is_ap;
    strncpy(ctx->ssid, ssid, sizeof(ctx->ssid) - 1u);
    strncpy(ctx->password, password, sizeof(ctx->password) - 1u);
    asm_mem_copy(ctx->own_addr, own_addr, 6u);
    asm_mem_copy(ctx->peer_addr, peer_addr, 6u);
    ctx->peer_commit_seen = 0;
    ctx->send_confirm = 0u;

    return sae_derive_pwe_ecc(ctx);
}

fl_result_t fl_net_wifi_sae_dragonfly_init_sta(fl_net_wifi_sae_dragonfly_ctx_t *ctx,
                                               const char *ssid, const char *password,
                                               const uint8_t sta_mac[6], const uint8_t bssid[6])
{
    return sae_dragonfly_init_common(ctx, 0, ssid, password, sta_mac, bssid);
}

fl_result_t fl_net_wifi_sae_dragonfly_init_ap(fl_net_wifi_sae_dragonfly_ctx_t *ctx,
                                              const char *ssid, const char *password,
                                              const uint8_t ap_bssid[6], const uint8_t sta_mac[6])
{
    return sae_dragonfly_init_common(ctx, 1, ssid, password, ap_bssid, sta_mac);
}

fl_result_t fl_net_wifi_sae_dragonfly_build_commit(fl_net_wifi_sae_dragonfly_ctx_t *ctx,
                                                   const uint8_t *anticlogging_token,
                                                   size_t anticlogging_len, uint8_t *body,
                                                   size_t body_cap, size_t *body_len_out)
{
    if (!ctx)
        return FL_RESULT_INVAL;
    if (BN_is_zero(ctx->own_scalar)) {
        if (sae_prepare_commit(ctx) != FL_RESULT_OK)
            return FL_RESULT_ERR;
    }
    return sae_write_commit_body(ctx, anticlogging_token, anticlogging_len, body, body_cap,
                                 body_len_out);
}

fl_result_t fl_net_wifi_sae_dragonfly_rx_commit(fl_net_wifi_sae_dragonfly_ctx_t *ctx,
                                              const uint8_t *body, size_t body_len)
{
    uint8_t k_buf[32];

    if (!ctx || !body)
        return FL_RESULT_INVAL;
    if (sae_parse_commit_body(ctx, body, body_len, NULL) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (sae_derive_k(ctx, k_buf) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    fl_net_wifi_crypto_memzero(k_buf, sizeof(k_buf));
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_sae_dragonfly_build_confirm(fl_net_wifi_sae_dragonfly_ctx_t *ctx,
                                                    uint8_t *body, size_t body_cap,
                                                    size_t *body_len_out)
{
    uint8_t confirm[SAE_CONFIRM_LEN];

    if (!ctx || !body || !body_len_out)
        return FL_RESULT_INVAL;
    if (body_cap < FL_NET_WIFI_SAE_CONFIRM_BODY_LEN)
        return FL_RESULT_INVAL;
    if (!ctx->peer_commit_seen)
        return FL_RESULT_ERR;

    ctx->send_confirm++;
    if (sae_build_confirm_value(ctx, confirm) != FL_RESULT_OK)
        return FL_RESULT_ERR;

    fl_net_put_u16_le(body, ctx->send_confirm);
    asm_mem_copy(body + 2u, confirm, sizeof(confirm));
    *body_len_out = FL_NET_WIFI_SAE_CONFIRM_BODY_LEN;
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_sae_dragonfly_verify_confirm(fl_net_wifi_sae_dragonfly_ctx_t *ctx,
                                                     const uint8_t *body, size_t body_len,
                                                     uint8_t pmk_out[FL_NET_WIFI_PMK_LEN])
{
    uint8_t expected[SAE_CONFIRM_LEN];
    uint8_t scalar_own[SAE_SCALAR_WIRE_LEN];
    uint8_t scalar_peer[SAE_SCALAR_WIRE_LEN];
    uint8_t element_own[SAE_ELEMENT_WIRE_LEN];
    uint8_t element_peer[SAE_ELEMENT_WIRE_LEN];
    uint16_t peer_send;
    fl_result_t rc = FL_RESULT_ERR;

    if (!ctx || !body || body_len < FL_NET_WIFI_SAE_CONFIRM_BODY_LEN || !pmk_out)
        return FL_RESULT_INVAL;
    if (!ctx->peer_commit_seen)
        return FL_RESULT_ERR;

    peer_send = fl_net_get_u16_le(body);

    if (sae_bn_to_bin_be(ctx->own_scalar, scalar_own, sizeof(scalar_own)) != FL_RESULT_OK ||
        sae_bn_to_bin_be(ctx->peer_scalar, scalar_peer, sizeof(scalar_peer)) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (sae_point_to_bin_xy(ctx->group, ctx->own_element, element_own, sizeof(element_own)) !=
            FL_RESULT_OK ||
        sae_point_to_bin_xy(ctx->group, ctx->peer_element, element_peer, sizeof(element_peer)) !=
            FL_RESULT_OK)
        return FL_RESULT_ERR;

    if (sae_build_confirm_value_for_peer(ctx, peer_send, scalar_peer, scalar_own, element_peer,
                                         element_own, expected) != FL_RESULT_OK)
        goto done;
    if (memcmp(expected, body + 2u, SAE_CONFIRM_LEN) != 0)
        goto done;

    asm_mem_copy(pmk_out, ctx->pmk, SAE_PMK_LEN);
    rc = FL_RESULT_OK;

done:
    fl_net_wifi_crypto_memzero(expected, sizeof(expected));
    fl_net_wifi_crypto_memzero(scalar_own, sizeof(scalar_own));
    fl_net_wifi_crypto_memzero(scalar_peer, sizeof(scalar_peer));
    fl_net_wifi_crypto_memzero(element_own, sizeof(element_own));
    fl_net_wifi_crypto_memzero(element_peer, sizeof(element_peer));
    return rc;
}

fl_result_t fl_net_wifi_sae_dragonfly_selftest(void)
{
    static const uint8_t sta_mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    static const uint8_t ap_mac[6] = {0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0x01};
    fl_net_wifi_sae_dragonfly_ctx_t *sta = NULL;
    fl_net_wifi_sae_dragonfly_ctx_t *ap = NULL;
    uint8_t sta_commit[128];
    uint8_t ap_commit[128];
    uint8_t sta_confirm[64];
    uint8_t ap_confirm[64];
    uint8_t pmk_sta[32];
    uint8_t pmk_ap[32];
    size_t sta_commit_len = 0;
    size_t ap_commit_len = 0;
    size_t sta_confirm_len = 0;
    size_t ap_confirm_len = 0;
    fl_result_t rc = FL_RESULT_ERR;

    if (fl_net_wifi_sae_dragonfly_ctx_create(&sta) != FL_RESULT_OK ||
        fl_net_wifi_sae_dragonfly_ctx_create(&ap) != FL_RESULT_OK)
        goto done;

    if (fl_net_wifi_sae_dragonfly_init_sta(sta, "DragonTest", "secret-psk", sta_mac, ap_mac) !=
            FL_RESULT_OK ||
        fl_net_wifi_sae_dragonfly_init_ap(ap, "DragonTest", "secret-psk", ap_mac, sta_mac) !=
            FL_RESULT_OK)
        goto done;

    if (fl_net_wifi_sae_dragonfly_build_commit(sta, NULL, 0u, sta_commit, sizeof(sta_commit),
                                               &sta_commit_len) != FL_RESULT_OK)
        goto done;
    if (fl_net_wifi_sae_dragonfly_build_commit(ap, NULL, 0u, ap_commit, sizeof(ap_commit),
                                               &ap_commit_len) != FL_RESULT_OK)
        goto done;

    if (fl_net_wifi_sae_dragonfly_rx_commit(sta, ap_commit, ap_commit_len) != FL_RESULT_OK ||
        fl_net_wifi_sae_dragonfly_rx_commit(ap, sta_commit, sta_commit_len) != FL_RESULT_OK)
        goto done;

    if (fl_net_wifi_sae_dragonfly_build_confirm(sta, sta_confirm, sizeof(sta_confirm),
                                                &sta_confirm_len) != FL_RESULT_OK ||
        fl_net_wifi_sae_dragonfly_build_confirm(ap, ap_confirm, sizeof(ap_confirm),
                                                &ap_confirm_len) != FL_RESULT_OK)
        goto done;

    if (fl_net_wifi_sae_dragonfly_verify_confirm(sta, ap_confirm, ap_confirm_len, pmk_sta) !=
            FL_RESULT_OK ||
        fl_net_wifi_sae_dragonfly_verify_confirm(ap, sta_confirm, sta_confirm_len, pmk_ap) !=
            FL_RESULT_OK)
        goto done;

    if (memcmp(pmk_sta, pmk_ap, sizeof(pmk_sta)) != 0)
        goto done;

    rc = FL_RESULT_OK;

done:
    fl_net_wifi_sae_dragonfly_ctx_destroy(sta);
    fl_net_wifi_sae_dragonfly_ctx_destroy(ap);
    fl_net_wifi_crypto_memzero(pmk_sta, sizeof(pmk_sta));
    fl_net_wifi_crypto_memzero(pmk_ap, sizeof(pmk_ap));
    return rc;
}

/* --- Lab PMK fingerprint + KDF self-test (unchanged contract surface) --- */

fl_result_t fl_net_wifi_sae_derive_pmk(const char *ssid, const char *passphrase, uint8_t *pmk_out,
                                       size_t pmk_cap) {
    uint8_t key[FL_WIFI_PASSPHRASE_MAX + FL_WIFI_SSID_MAX];
    size_t pass_len;
    size_t ssid_len;

    if (!ssid || !passphrase || !pmk_out || pmk_cap < FL_NET_WIFI_PMK_LEN)
        return FL_RESULT_INVAL;
    pass_len = strlen(passphrase);
    ssid_len = strlen(ssid);
    if (pass_len < 1u || ssid_len < 1u || ssid_len > FL_WIFI_SSID_MAX)
        return FL_RESULT_INVAL;
    if (pass_len >= FL_WIFI_PASSPHRASE_MAX)
        return FL_RESULT_INVAL;
    if (pass_len + ssid_len > sizeof(key))
        return FL_RESULT_INVAL;

    asm_mem_copy(key, passphrase, pass_len);
    asm_mem_copy(key + pass_len, ssid, ssid_len);
    if (fl_net_wifi_crypto_sae_kdf(key, pass_len + ssid_len, "SAE KDF PMK",
                                   (const uint8_t *)ssid, ssid_len, pmk_out,
                                   FL_NET_WIFI_PMK_LEN) != FL_RESULT_OK) {
        fl_net_wifi_crypto_memzero(key, sizeof(key));
        return FL_RESULT_ERR;
    }
    fl_net_wifi_crypto_memzero(key, sizeof(key));
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_sae_rfc7664_kdf_selftest(void) {
    static const uint8_t key[] = "password";
    static const uint8_t context[] = "IEEE";
    uint8_t a[32];
    uint8_t b[32];
    uint8_t c[32];

    if (fl_net_wifi_crypto_sae_kdf(key, 8u, "SAE KDF test", context, 4u, a, sizeof(a)) !=
            FL_RESULT_OK ||
        fl_net_wifi_crypto_sae_kdf(key, 8u, "SAE KDF test", context, 4u, b, sizeof(b)) !=
            FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (memcmp(a, b, sizeof(a)) != 0)
        return FL_RESULT_ERR;
    if (fl_net_wifi_crypto_sae_kdf(key, 7u, "SAE KDF test", context, 4u, c, sizeof(c)) !=
        FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (memcmp(a, c, sizeof(a)) == 0)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}
