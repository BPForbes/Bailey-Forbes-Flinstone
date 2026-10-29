/**
 * test_secret_compare.c - the one secret compare / secure wipe (#384 D10):
 * fl_user_db_secret_matches, fl_user_db_open_key / fl_user_db_open_box wipes, and
 * fl_password_verify (raw-byte compare, either hex case). Passwords and secret bytes are
 * drawn at run time; 0xAA is only a wipe sentinel. Messages never print secrets or the
 * results of secret-bearing calls.
 */
#include "user_db.h"
#include "password_hash.h"
#include "fl/hex.h"
#include "fl/random.h"
#include <openssl/sha.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SECRET_LEN FL_USER_SIGNING_SECRET_LEN
#define SENTINEL 0xAA

#define ASSERT(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        return 1; \
    } \
} while (0)

/* The link line wraps asm_mem_secure_zero (-Wl,--wrap) so a test can see which buffer was
 * wiped last with the SECURE zero; a plain mem_domain_zero never reaches it. */
extern void __real_asm_mem_secure_zero(void *p, size_t n);
static const void *g_wipe_ptr;
static size_t g_wipe_len;
static unsigned g_wipe_calls;

void __wrap_asm_mem_secure_zero(void *p, size_t n) {
    g_wipe_ptr = p;
    g_wipe_len = n;
    g_wipe_calls++;
    __real_asm_mem_secure_zero(p, n);
}

#define WIPE_RESET() do { g_wipe_ptr = NULL; g_wipe_len = 0; g_wipe_calls = 0; } while (0)
#define WIPED_LAST(buf, n) (g_wipe_ptr == (const void *)(buf) && g_wipe_len == (n))

static fl_user_db_t g_db;
static fl_user_db_t g_copy;

static int temp_db(char *out, size_t cap) {
    char tmpl[] = "/tmp/fl_secretcmp_testXXXXXX";
    int fd = mkstemp(tmpl);

    if (fd < 0)
        return -1;
    close(fd);
    unlink(tmpl);
    snprintf(out, cap, "%s.db", tmpl);
    return 0;
}

/* 2 * bytes lower-case hex digits of fresh random bytes (out holds 2 * bytes + 1). */
static int random_text(char *out, size_t bytes) {
    unsigned char raw[16];

    if (bytes > sizeof raw || fl_random_bytes(raw, bytes) != 0)
        return -1;
    fl_hex_encode(raw, bytes, out);
    memset(raw, 0, sizeof raw);
    return 0;
}

static int all_zero(const unsigned char *p, size_t n) {
    size_t i;

    for (i = 0; i < n; i++)
        if (p[i])
            return 0;
    return 1;
}

static int all_sentinel(const unsigned char *p, size_t n) {
    size_t i;

    for (i = 0; i < n; i++)
        if (p[i] != SENTINEL)
            return 0;
    return 1;
}

static fl_user_record_t *copy_rec(const char *key_id) {
    size_t i;

    for (i = 0; i < g_copy.count; i++)
        if (strcmp(g_copy.users[i].key_id, key_id) == 0)
            return &g_copy.users[i];
    return NULL;
}

static int test_secret_matches(void) {
    char path[256];
    char pw_a[40], pw_b[40];
    unsigned char sk_a[SECRET_LEN], sk_b[SECRET_LEN], bx_a[SECRET_LEN];
    unsigned char probe[SECRET_LEN], zero[SECRET_LEN];
    size_t i;
    fl_user_record_t *rec;

    ASSERT(temp_db(path, sizeof path) == 0);
    ASSERT(random_text(pw_a, 12) == 0 && random_text(pw_b, 12) == 0);
    ASSERT(fl_user_db_load(&g_db, path) == FL_RESULT_OK);
    ASSERT(fl_user_db_add_user_under(&g_db, "Root", "Ann", pw_a, 3000, 0, 0) == FL_RESULT_OK);
    ASSERT(fl_user_db_add_user_under(&g_db, "Root", "Bob", pw_b, 3001, 0, 0) == FL_RESULT_OK);
    ASSERT(fl_user_db_open_key(&g_db, "Ann", pw_a, sk_a) == FL_RESULT_OK);
    ASSERT(fl_user_db_open_key(&g_db, "Bob", pw_b, sk_b) == FL_RESULT_OK);
    ASSERT(fl_user_db_open_box(&g_db, "Ann", pw_a, bx_a) == FL_RESULT_OK);

    ASSERT(fl_user_db_secret_matches(&g_db, "Root.Ann", FL_USER_SECRET_SIGNING, sk_a) == 1);
    ASSERT(fl_user_db_secret_matches(&g_db, "Root.Bob", FL_USER_SECRET_SIGNING, sk_b) == 1);
    ASSERT(fl_user_db_secret_matches(&g_db, "Root.Ann", FL_USER_SECRET_BOX, bx_a) == 1);

    /* A flipped bit in any byte is a mismatch (the box flip avoids X25519's clamped bits). */
    for (i = 0; i < SECRET_LEN; i++) {
        memcpy(probe, sk_a, sizeof probe);
        probe[i] ^= 0x01;
        ASSERT(fl_user_db_secret_matches(&g_db, "Root.Ann", FL_USER_SECRET_SIGNING, probe) == 0);
        memcpy(probe, bx_a, sizeof probe);
        probe[i] ^= 0x10; /* X25519 clamps bits 0-2 of byte 0 and 6-7 of byte 31 */
        ASSERT(fl_user_db_secret_matches(&g_db, "Root.Ann", FL_USER_SECRET_BOX, probe) == 0);
    }
    memset(zero, 0, sizeof zero);
    ASSERT(fl_user_db_secret_matches(&g_db, "Root.Ann", FL_USER_SECRET_SIGNING, zero) == 0);
    ASSERT(fl_user_db_secret_matches(&g_db, "Root.Ann", FL_USER_SECRET_BOX, zero) == 0);

    /* Someone else's key, the other kind, an unknown id, a bad kind. */
    ASSERT(fl_user_db_secret_matches(&g_db, "Root.Ann", FL_USER_SECRET_SIGNING, sk_b) == 0);
    ASSERT(fl_user_db_secret_matches(&g_db, "Root.Ann", FL_USER_SECRET_BOX, sk_a) == 0);
    ASSERT(fl_user_db_secret_matches(&g_db, "Root.Ann", FL_USER_SECRET_SIGNING, bx_a) == 0);
    ASSERT(fl_user_db_secret_matches(&g_db, "Root.Nobody", FL_USER_SECRET_SIGNING, sk_a) == 0);
    ASSERT(fl_user_db_secret_matches(&g_db, "Root.Ann", (fl_user_secret_kind_t)7, sk_a) == 0);

    /* NULL arguments. */
    ASSERT(fl_user_db_secret_matches(NULL, "Root.Ann", FL_USER_SECRET_SIGNING, sk_a) == 0);
    ASSERT(fl_user_db_secret_matches(&g_db, NULL, FL_USER_SECRET_SIGNING, sk_a) == 0);
    ASSERT(fl_user_db_secret_matches(&g_db, "Root.Ann", FL_USER_SECRET_SIGNING, NULL) == 0);

    /* A record without that kind of key matches nothing. */
    g_copy = g_db;
    rec = copy_rec("Root.Ann");
    ASSERT(rec != NULL);
    rec->has_key = 0;
    ASSERT(fl_user_db_secret_matches(&g_copy, "Root.Ann", FL_USER_SECRET_SIGNING, sk_a) == 0);
    ASSERT(fl_user_db_secret_matches(&g_copy, "Root.Ann", FL_USER_SECRET_BOX, bx_a) == 1);
    rec->has_box = 0;
    ASSERT(fl_user_db_secret_matches(&g_copy, "Root.Ann", FL_USER_SECRET_BOX, bx_a) == 0);

    memset(sk_a, 0, sizeof sk_a);
    memset(sk_b, 0, sizeof sk_b);
    memset(bx_a, 0, sizeof bx_a);
    unlink(path);
    return 0;
}

static int test_open_wipes(void) {
    char path[256];
    char pw[40], wrong[40];
    unsigned char out[SECRET_LEN];
    fl_user_record_t *rec;

    ASSERT(temp_db(path, sizeof path) == 0);
    ASSERT(random_text(pw, 12) == 0 && random_text(wrong, 12) == 0);
    ASSERT(fl_user_db_load(&g_db, path) == FL_RESULT_OK);
    ASSERT(fl_user_db_add_user_under(&g_db, "Root", "Ann", pw, 3000, 0, 0) == FL_RESULT_OK);

    /* Good password: opens, and the secret matches. */
    memset(out, SENTINEL, sizeof out);
    ASSERT(fl_user_db_open_key(&g_db, "Ann", pw, out) == FL_RESULT_OK);
    ASSERT(fl_user_db_secret_matches(&g_db, "Root.Ann", FL_USER_SECRET_SIGNING, out) == 1);
    memset(out, SENTINEL, sizeof out);
    ASSERT(fl_user_db_open_box(&g_db, "Ann", pw, out) == FL_RESULT_OK);
    ASSERT(fl_user_db_secret_matches(&g_db, "Root.Ann", FL_USER_SECRET_BOX, out) == 1);

    /* Wrong password: ACCES, and nothing but zeros is left in the output. */
    memset(out, SENTINEL, sizeof out);
    WIPE_RESET();
    ASSERT(fl_user_db_open_key(&g_db, "Ann", wrong, out) == FL_RESULT_ACCES);
    ASSERT(all_zero(out, sizeof out) && WIPED_LAST(out, sizeof out));
    memset(out, SENTINEL, sizeof out);
    WIPE_RESET();
    ASSERT(fl_user_db_open_box(&g_db, "Ann", wrong, out) == FL_RESULT_ACCES);
    ASSERT(all_zero(out, sizeof out) && WIPED_LAST(out, sizeof out));

    /* Right password but a stored public key that does not name the sealed secret. */
    g_copy = g_db;
    rec = copy_rec("Root.Ann");
    ASSERT(rec != NULL);
    rec->signing_public[0] ^= 0x01;
    rec->box_public[31] ^= 0x01;
    memset(out, SENTINEL, sizeof out);
    WIPE_RESET();
    ASSERT(fl_user_db_open_key(&g_copy, "Ann", pw, out) == FL_RESULT_ERR);
    ASSERT(all_zero(out, sizeof out) && WIPED_LAST(out, sizeof out));
    memset(out, SENTINEL, sizeof out);
    WIPE_RESET();
    ASSERT(fl_user_db_open_box(&g_copy, "Ann", pw, out) == FL_RESULT_ERR);
    ASSERT(all_zero(out, sizeof out) && WIPED_LAST(out, sizeof out));

    /* A sealed blob that does not open at all is also wiped (generic error). */
    g_copy = g_db;
    rec = copy_rec("Root.Ann");
    ASSERT(rec != NULL);
    rec->signing_sealed[0] ^= 0xFF;
    memset(out, SENTINEL, sizeof out);
    WIPE_RESET();
    ASSERT(fl_user_db_open_key(&g_copy, "Ann", pw, out) != FL_RESULT_OK);
    ASSERT(all_zero(out, sizeof out) && WIPED_LAST(out, sizeof out));

    /* No key / no box / no account / bad arguments: NOENT or INVAL, output untouched. */
    g_copy = g_db;
    rec = copy_rec("Root.Ann");
    ASSERT(rec != NULL);
    rec->has_key = 0;
    rec->has_box = 0;
    memset(out, SENTINEL, sizeof out);
    ASSERT(fl_user_db_open_key(&g_copy, "Ann", pw, out) == FL_RESULT_NOENT);
    ASSERT(fl_user_db_open_box(&g_copy, "Ann", pw, out) == FL_RESULT_NOENT);
    ASSERT(fl_user_db_open_key(&g_copy, "Nobody", pw, out) == FL_RESULT_NOENT);
    ASSERT(fl_user_db_open_key(&g_copy, "Ann", NULL, out) == FL_RESULT_INVAL);
    ASSERT(fl_user_db_open_box(&g_copy, "Ann", pw, NULL) == FL_RESULT_INVAL);
    ASSERT(all_sentinel(out, sizeof out));

    memset(out, 0, sizeof out);
    unlink(path);
    return 0;
}

static void to_upper(char *s) {
    for (; *s; s++)
        *s = (char)toupper((unsigned char)*s);
}

static int test_password_verify(void) {
    char pw[40], other[40];
    char salt[FL_PASSWORD_SALT_HEX_CHARS + 1];
    char rec[FL_PASSWORD_HASH_RECORD_MAX];
    char bad[FL_PASSWORD_HASH_RECORD_MAX];
    char longer[2 * FL_PASSWORD_HASH_RECORD_MAX];
    unsigned char raw_salt[FL_PASSWORD_SALT_BYTES];
    unsigned char *mat;
    unsigned char d[32];
    char legacy_hex[65];
    char *dollar;
    size_t i;

    ASSERT(random_text(pw, 12) == 0 && random_text(other, 12) == 0);
    ASSERT(fl_password_generate_salt_hex(salt, sizeof salt) == 0);
    ASSERT(fl_password_hash_password(pw, salt, rec, sizeof rec) == 0);

    /* Secure wipes per verify: a PBKDF2 record is 1 (hex_digest_verify's decoded copy) + 3
     * (computed, stretched, legacy on the single exit) = 4. A bare legacy 64-hex digest is
     * 2 (hex_digest_verify twice) + 2 (the salt||password copy in hash_legacy_bin, called for
     * the stretched and the single variant) + 3 = 7. Fewer means a secret was left unwiped. */
    WIPE_RESET();
    ASSERT(fl_password_verify(pw, salt, rec) == 1);
    ASSERT(g_wipe_calls == 4 && g_wipe_len == 32);
    WIPE_RESET();
    ASSERT(fl_password_verify(other, salt, rec) == 0);
    ASSERT(g_wipe_calls == 4);
    ASSERT(fl_password_verify(NULL, salt, rec) == 0);

    /* Truncated, extended and non-hex digests are refused. */
    snprintf(bad, sizeof bad, "%s", rec);
    bad[strlen(bad) - 1] = '\0';
    ASSERT(fl_password_verify(pw, salt, bad) == 0);
    snprintf(longer, sizeof longer, "%s0", rec); /* 65 digits */
    ASSERT(fl_password_verify(pw, salt, longer) == 0);
    snprintf(bad, sizeof bad, "%s", rec);
    dollar = strrchr(bad, '$');
    ASSERT(dollar != NULL);
    dollar[1] = 'g';
    ASSERT(fl_password_verify(pw, salt, bad) == 0);

    /* One flipped hex digit anywhere in the digest is refused. */
    for (i = 0; i < 64; i++) {
        snprintf(bad, sizeof bad, "%s", rec);
        dollar = strrchr(bad, '$') + 1;
        dollar[i] = dollar[i] == '0' ? '1' : '0';
        ASSERT(fl_password_verify(pw, salt, bad) == 0);
    }

    /* Behaviour change (D10): the compare is on raw bytes, so upper-case hex verifies. */
    snprintf(bad, sizeof bad, "%s", rec);
    dollar = strrchr(bad, '$') + 1;
    to_upper(dollar);
    ASSERT(fl_password_verify(pw, salt, bad) == 1);
    ASSERT(fl_password_verify(other, salt, bad) == 0);

    /* Legacy forms: bare 64 hex of salt||password hashed once, and stretched 60000 times. */
    {
        size_t plen = strlen(pw);
        unsigned int n;

        ASSERT(fl_hex_decode(salt, raw_salt, sizeof raw_salt) == 0);
        mat = (unsigned char *)malloc(sizeof raw_salt + plen);
        ASSERT(mat != NULL);
        memcpy(mat, raw_salt, sizeof raw_salt);
        memcpy(mat + sizeof raw_salt, pw, plen);
        SHA256(mat, sizeof raw_salt + plen, d);
        free(mat);
        fl_hex_encode(d, 32, legacy_hex);
        WIPE_RESET();
        ASSERT(fl_password_verify(pw, salt, legacy_hex) == 1);
        ASSERT(g_wipe_calls == 7);
        ASSERT(fl_password_verify(other, salt, legacy_hex) == 0);
        for (n = 1; n < 60000u; n++)
            SHA256(d, 32, d);
        fl_hex_encode(d, 32, legacy_hex);
        ASSERT(fl_password_verify(pw, salt, legacy_hex) == 1);
        ASSERT(fl_password_verify(other, salt, legacy_hex) == 0);
        to_upper(legacy_hex);
        ASSERT(fl_password_verify(pw, salt, legacy_hex) == 1);
    }

    /* Wrong lengths and non-hex legacy text. */
    ASSERT(fl_password_verify(pw, salt, "") == 0);
    memset(legacy_hex, 'z', 64);
    legacy_hex[64] = '\0';
    ASSERT(fl_password_verify(pw, salt, legacy_hex) == 0);

    memset(d, 0, sizeof d);
    return 0;
}

int main(void) {
    int rc = 0;

    (void)setenv("FL_USERS_LAB_DEFAULTS", "1", 1);
#define RUN(t) do { \
        if (t() != 0) { rc = 1; fprintf(stderr, "%s failed\n", #t); } \
        else printf("%s... OK\n", #t); \
    } while (0)
    RUN(test_secret_matches);
    RUN(test_open_wipes);
    RUN(test_password_verify);
    if (rc == 0)
        printf("test_secret_compare: all passed\n");
    return rc;
}
