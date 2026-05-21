#ifndef FL_PASSWORD_HASH_H
#define FL_PASSWORD_HASH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FL_PASSWORD_SALT_BYTES 16u
#define FL_PASSWORD_SALT_HEX_CHARS 32u
/** PBKDF2-HMAC-SHA256 digest length (hex chars). */
#define FL_PASSWORD_HASH_HEX_CHARS 64u
/** Stored record: "pbkdf2-sha256$<iter>$" + 64 hex (see password_hash.cpp). */
#define FL_PASSWORD_HASH_RECORD_MAX 96u
#define FL_PASSWORD_PBKDF2_ITERATIONS 100000u

/** Generate random salt (hex out length FL_PASSWORD_SALT_HEX_CHARS + 1). */
int fl_password_generate_salt_hex(char *salt_hex, size_t salt_hex_size);

/** Hash password; writes PBKDF2 record (prefix + iterations + hex digest) + NUL. */
int fl_password_hash_password(const char *password, const char *salt_hex,
                              char *hash_hex, size_t hash_hex_size);

/** Constant-time verify; accepts PBKDF2 record or legacy 64-hex stretch/single SHA-256. */
int fl_password_verify(const char *password, const char *salt_hex, const char *hash_hex);

#ifdef __cplusplus
}
#endif

#endif /* FL_PASSWORD_HASH_H */
