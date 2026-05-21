#ifndef FL_PASSWORD_HASH_H
#define FL_PASSWORD_HASH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FL_PASSWORD_SALT_BYTES 16u
#define FL_PASSWORD_HASH_HEX_CHARS 64u
#define FL_PASSWORD_SALT_HEX_CHARS 32u
/** Iterated SHA-256 rounds after salt||password (new hashes); legacy single-round still verifies. */
#define FL_PASSWORD_STRETCH_ITERATIONS 60000u

/** Generate random salt (hex out length FL_PASSWORD_SALT_HEX_CHARS + 1). */
int fl_password_generate_salt_hex(char *salt_hex, size_t salt_hex_size);

/** Hash password with salt (hex); writes 64-char hex hash + NUL. */
int fl_password_hash_password(const char *password, const char *salt_hex,
                              char *hash_hex, size_t hash_hex_size);

/** Constant-time compare of stored hash hex to recomputed hash. */
int fl_password_verify(const char *password, const char *salt_hex, const char *hash_hex);

#ifdef __cplusplus
}
#endif

#endif /* FL_PASSWORD_HASH_H */
