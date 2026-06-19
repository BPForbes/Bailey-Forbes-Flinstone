#ifndef SERVER_FILE_EXPIRE_H
#define SERVER_FILE_EXPIRE_H

#include <stddef.h>
#include <stdint.h>

/**
 * Parse -Ex argument: duration (2s, 2h, 2d, 2m, bare seconds) or UTC datetime
 * mm-dd-yyyy [hh:mm[:ss]] (seconds default :00; time defaults to midnight UTC).
 * Returns 0 on success and sets *expires_at_out to unix seconds (UTC).
 */
int fl_server_file_parse_expire_arg(const char *arg, uint64_t *expires_at_out);

/** Format expires_at for user display (UTC). */
void fl_server_file_format_expires_utc(uint64_t expires_at, char *out, size_t out_cap);

#endif /* SERVER_FILE_EXPIRE_H */
