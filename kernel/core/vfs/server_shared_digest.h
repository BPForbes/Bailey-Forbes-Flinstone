#ifndef SERVER_SHARED_DIGEST_H
#define SERVER_SHARED_DIGEST_H

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

/** SHA-256 of `data`/`len` as lowercase hex in `out` (cap >= 65). */
fl_result_t fl_server_shared_sha256_hex(const uint8_t *data,
                                        size_t len,
                                        char *out,
                                        size_t out_cap);

#endif /* SERVER_SHARED_DIGEST_H */
